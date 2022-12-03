#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include "include/eink_driver.h"
#include "include/eink_sys_source.h"


struct disp_layer_config_inner eink_para[16];
struct disp_layer_config2 eink_lyr_cfg2[16];

struct eink_driver_info g_eink_drvdata;

u32 eink_dbg_info;

static ssize_t eink_debug_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "debug=%d\n", eink_dbg_info);
}

static ssize_t eink_debug_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	if (strncasecmp(buf, "1", 1) == 0)
		eink_dbg_info = 1;
	else if (strncasecmp(buf, "0", 1) == 0)
		eink_dbg_info = 0;
	else if (strncasecmp(buf, "2", 1) == 0)
		eink_dbg_info = 2;
	else if (strncasecmp(buf, "3", 1) == 0)
		eink_dbg_info = 3;
	else
		pr_err("Error input!\n");

	return count;
}

static ssize_t eink_sys_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct eink_manager *eink_mgr = NULL;
	struct disp_manager *disp_mgr = NULL;
	ssize_t count = 0;
	u32 width = 0, height = 0;
	int num_layers, layer_id;
	int num_chans, chan_id;

	eink_mgr = get_eink_manager();
	if (eink_mgr == NULL)
		return 0;
	disp_mgr = disp_get_layer_manager(0);
	if (disp_mgr == NULL)
		return 0;

	eink_mgr->get_resolution(eink_mgr, &width, &height);

	if (eink_mgr->enable_flag == false) {
		pr_warn("eink not enable yet!\n");
		return 0;
	}

	count += sprintf(buf + count, "eink_rate %d hz, panel_rate %d hz, fps:%d\n",
			eink_mgr->get_clk_rate(eink_mgr->ee_clk),
			eink_mgr->get_clk_rate(eink_mgr->panel_clk), eink_mgr->get_fps(eink_mgr));
	count += eink_mgr->dump_config(eink_mgr, buf + count);
	/* output */
	count += sprintf(buf + count,
			"\teink output\n");

	num_chans = bsp_disp_feat_get_num_channels(0);

	/* layer info */
	for (chan_id = 0; chan_id < num_chans; chan_id++) {
		num_layers =
			bsp_disp_feat_get_num_layers_by_chn(0,
					chan_id);
		for (layer_id = 0; layer_id < num_layers; layer_id++) {
			struct disp_layer *lyr = NULL;
			struct disp_layer_config config;

			lyr = disp_get_layer(0, chan_id, layer_id);
			config.channel = chan_id;
			config.layer_id = layer_id;
			disp_mgr->get_layer_config(disp_mgr, &config, 1);
			if (lyr && (true == config.enable) && lyr->dump)
				count += lyr->dump(lyr, buf + count);
		}
	}
	return count;
}

static ssize_t eink_sys_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(debug, 0660, eink_debug_show, eink_debug_store);
static DEVICE_ATTR(sys, 0660, eink_sys_show, eink_sys_store);

static struct attribute *eink_attributes[] = {
	&dev_attr_debug.attr,
	&dev_attr_sys.attr,
	NULL
};

static struct attribute_group eink_attribute_group = {
	.name = "attr",
	.attrs = eink_attributes
};

s32 eink_set_print_level(u32 level)
{
	EINK_INFO_MSG("print level = 0x%x\n", level);
	eink_dbg_info = level;

	return 0;
}

s32 eink_get_print_level(void)
{
	return eink_dbg_info;
}

/* unload resources of eink device */
static void eink_unload_resource(struct eink_driver_info *drvdata)
{
	if (!IS_ERR_OR_NULL(drvdata->init_para.panel_clk))
		clk_put(drvdata->init_para.panel_clk);
	if (!IS_ERR_OR_NULL(drvdata->init_para.ee_clk))
		clk_put(drvdata->init_para.ee_clk);
	if (!IS_ERR_OR_NULL(drvdata->init_para.de_clk))
		clk_put(drvdata->init_para.de_clk);
	if (drvdata->init_para.ee_reg_base)
		iounmap(drvdata->init_para.ee_reg_base);
	if (drvdata->init_para.de_reg_base)
		iounmap(drvdata->init_para.de_reg_base);
	return;
}

static int eink_init(struct init_para *para)
{
	eink_get_sys_config(para);
	fmt_convert_mgr_init(para);
	eink_mgr_init(para);
	return 0;
}

static void eink_exit(void)
{
/*
	fmt_convert_mgr_exit(para);
	eink_mgr_exit(para);
*/
	return;
}

//static u64 eink_dmamask = DMA_BIT_MASK(32);
static int eink_probe(struct platform_device *pdev)
{
	int ret = 0, counter = 0;
	struct device_node *node = pdev->dev.of_node;
	struct eink_driver_info *drvdata = NULL;

	if (!of_device_is_available(node)) {
		pr_err("EINK device is not configed!\n");
		return -ENODEV;
	}

	drvdata = &g_eink_drvdata;

	eink_set_print_level(EINK_PRINT_LEVEL);
	/* reg_base */
	drvdata->init_para.ee_reg_base = of_iomap(node, counter);
	if (!drvdata->init_para.ee_reg_base) {
		pr_err("of_iomap eink reg base failed!\n");
		ret =  -ENOMEM;
		goto err_out;
	}
	counter++;
	EINK_INFO_MSG("ee reg_base=0x%p\n", drvdata->init_para.ee_reg_base);

	drvdata->init_para.de_reg_base = of_iomap(node, counter);
	if (!drvdata->init_para.de_reg_base) {
		pr_err("of_iomap de wb reg base failed\n");
		ret =  -ENOMEM;
		goto err_out;
	}
	EINK_INFO_MSG("de reg_base=0x%p\n", drvdata->init_para.de_reg_base);

	/* clk */
	counter = 0;
	drvdata->init_para.de_clk = of_clk_get(node, counter);
	if (IS_ERR_OR_NULL(drvdata->init_para.de_clk)) {
		pr_err("get disp engine clock failed!\n");
		ret = PTR_ERR(drvdata->init_para.ee_clk);
		goto err_out;
	}
	counter++;
	drvdata->init_para.ee_clk = of_clk_get(node, counter);
	if (IS_ERR_OR_NULL(drvdata->init_para.ee_clk)) {
		pr_err("get eink engine clock failed!\n");
		ret = PTR_ERR(drvdata->init_para.ee_clk);
		goto err_out;
	}
	counter++;
	drvdata->init_para.panel_clk = of_clk_get(node, counter);
	if (IS_ERR_OR_NULL(drvdata->init_para.panel_clk)) {
		pr_err("get edma clk clock failed!\n");
		ret = PTR_ERR(drvdata->init_para.panel_clk);
		goto err_out;
	}
	/* irq */
	counter = 0;
	drvdata->init_para.ee_irq_no = irq_of_parse_and_map(node, counter);
	if (!drvdata->init_para.ee_irq_no) {
		pr_err("get eink irq failed!\n");
		ret = -EINVAL;
		goto err_out;
	}

	counter++;
	EINK_INFO_MSG("eink irq_no=%u\n", drvdata->init_para.ee_irq_no);

	drvdata->init_para.de_irq_no = irq_of_parse_and_map(node, counter);
	if (!drvdata->init_para.de_irq_no) {
		pr_err("get de irq failed!\n");
		ret = -EINVAL;
		goto err_out;
	}
	EINK_INFO_MSG("de irq_no=%u\n", drvdata->init_para.de_irq_no);

	drvdata->dmabuf_dev = &pdev->dev;
/*
	pdev->dev.dma_mask = &eink_dmamask;
	pdev->dev.coherent_dma_mask = eink_dmamask;
*/
	platform_set_drvdata(pdev, (void *)drvdata);

	eink_init(&drvdata->init_para);

	g_eink_drvdata.eink_mgr = get_eink_manager();

	pr_info("[EINK] probe finish!\n");

	return 0;

err_out:
	eink_unload_resource(drvdata);
	dev_err(&pdev->dev, "eink probe failed, errno %d!\n", ret);
	return ret;
}

static int eink_remove(struct platform_device *pdev)
{
	struct eink_driver_info *drvdata;

	dev_info(&pdev->dev, "%s\n", __func__);

	drvdata = platform_get_drvdata(pdev);
	if (drvdata != NULL) {
		eink_unload_resource(drvdata);
		platform_set_drvdata(pdev, NULL);
	} else
		pr_err("%s:drvdata is NULL!\n", __func__);

	pr_info("%s finish!\n", __func__);

	return 0;
}

static int eink_suspend(struct device *dev)
{
	int ret = 0;
	return ret;
}

int eink_resume(struct device *dev)
{
	int ret = 0;
	return ret;
}

int eink_open(struct inode *inode, struct file *file)
{
	int ret = 0;
	pr_info("%s open the device!\n", __func__);
	return ret;
}

int eink_release(struct inode *inode, struct file *file)
{
	int ret = 0;
	pr_info("%s finish!\n", __func__);
	return ret;
}

long eink_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	unsigned long karg[4];
	unsigned long ubuffer[4] = { 0 };
	struct eink_manager *eink_mgr = NULL;

	eink_mgr = g_eink_drvdata.eink_mgr;

	if (copy_from_user
			((void *)karg, (void __user *)arg, 4 * sizeof(unsigned long))) {
		pr_err("copy_from_user fail\n");
		return -EFAULT;
	}

	ubuffer[0] = *(unsigned long *)karg;
	ubuffer[1] = (*(unsigned long *)(karg + 1));
	ubuffer[2] = (*(unsigned long *)(karg + 2));
	ubuffer[3] = (*(unsigned long *)(karg + 3));

	switch (cmd) {
	case EINK_UPDATE_IMG:
		{
			s32 i = 0;
			struct eink_upd_cfg eink_cfg;

			if (!eink_mgr) {
				pr_err("there is no eink manager!\n");
				break;
			}

			memset(eink_lyr_cfg2, 0,
					16 * sizeof(struct disp_layer_config2));
			if (copy_from_user(eink_lyr_cfg2, (void __user *)ubuffer[2],
						sizeof(struct disp_layer_config2) * ubuffer[1])) {
				pr_err("copy_from_user fail\n");
				return -EFAULT;
			}

			memset(&eink_cfg, 0, sizeof(struct eink_upd_cfg));
			if (copy_from_user(&eink_cfg, (void __user *)ubuffer[0],
						sizeof(struct eink_upd_cfg))) {
				pr_err("copy_from_user fail\n");
				return -EFAULT;
			}

			for (i = 0; i < ubuffer[1]; i++)
				__disp_config2_transfer2inner(&eink_para[i],
						&eink_lyr_cfg2[i]);

			printk("\n");
			printk("%s:eink_cfg fmt = %d, mode =%d\n", __func__, eink_cfg.out_fmt, eink_cfg.upd_mode);

			if (eink_mgr->eink_update)
				ret = eink_mgr->eink_update(eink_mgr,
						(struct disp_layer_config_inner *)&eink_para[0],
						(unsigned int)ubuffer[1],
						(struct eink_upd_cfg *)&eink_cfg);
			break;
		}

	case EINK_SET_TEMP:
		{
			if (eink_mgr->set_temperature)
				ret = eink_mgr->set_temperature(eink_mgr, ubuffer[0]);
			break;
		}

	case EINK_GET_TEMP:
		{
			if (eink_mgr->get_temperature)
				ret = eink_mgr->get_temperature(eink_mgr);
			break;
		}

	case EINK_SET_GC_CNT:
		{
			if (eink_mgr->eink_set_global_clean_cnt)
				ret = eink_mgr->eink_set_global_clean_cnt(eink_mgr, ubuffer[0]);
			break;
		}

	default:
		pr_err("The cmd is err!\n");
		break;
	}
	return ret;

}

const struct file_operations eink_fops = {
	.owner = THIS_MODULE,
	.open = eink_open,
	.release = eink_release,
	.unlocked_ioctl = eink_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = eink_ioctl,
#endif
};

static const struct dev_pm_ops eink_pm_ops = {
	.suspend = eink_suspend,
	.resume = eink_resume,
};

static const struct of_device_id sunxi_eink_match[] = {
	{.compatible = "allwinner,sunxi-eink"},
	{},
};

static struct platform_driver eink_driver = {
	.probe = eink_probe,
	.remove = eink_remove,
	.driver = {
		.name = EINK_MODULE_NAME,
		.owner = THIS_MODULE,
		.pm = &eink_pm_ops,
		.of_match_table = sunxi_eink_match,
	},
};


static int __init eink_module_init(void)
{
	int ret = 0;
	struct eink_driver_info *drvdata = NULL;

	pr_info("[EINK]%s:\n", __func__);

	drvdata = &g_eink_drvdata;

	alloc_chrdev_region(&drvdata->devt, 0, 1, EINK_MODULE_NAME);
	drvdata->pcdev = cdev_alloc();
	cdev_init(drvdata->pcdev, &eink_fops);
	drvdata->pcdev->owner = THIS_MODULE;
	ret = cdev_add(drvdata->pcdev, drvdata->devt, 1);
	if (ret) {
		pr_err("eink cdev add major(%d) failed!\n", MAJOR(drvdata->devt));
		return -1;
	}
	drvdata->pclass = class_create(THIS_MODULE, EINK_MODULE_NAME);
	if (IS_ERR(drvdata->pclass)) {
		pr_err("eink create class error!\n");
		ret = PTR_ERR(drvdata->pclass);
		return ret;
	}

	drvdata->eink_dev = device_create(drvdata->pclass, NULL,
			drvdata->devt, NULL, EINK_MODULE_NAME);
	if (IS_ERR(drvdata->eink_dev)) {
		pr_err("eink device_create error!\n");
		ret = PTR_ERR(drvdata->eink_dev);
		return ret;
	}
	platform_driver_register(&eink_driver);

	ret = sysfs_create_group(&drvdata->eink_dev->kobj, &eink_attribute_group);
	if (ret < 0)
		pr_err("sysfs_create_file fail!\n");

	pr_info("[EINK]%s: finish\n", __func__);

	return 0;
}

static void __init eink_module_exit(void)
{
	struct eink_driver_info *drvdata = NULL;

	pr_info("[EINK]%s:\n", __func__);

	drvdata = &g_eink_drvdata;

	eink_exit();
	platform_driver_unregister(&eink_driver);
	device_destroy(drvdata->pclass, drvdata->devt);
	class_destroy(drvdata->pclass);
	cdev_del(drvdata->pcdev);
	unregister_chrdev_region(drvdata->devt, 1);
	return;
}

late_initcall(eink_module_init);
module_exit(eink_module_exit);

/* module_platform_driver(eink_driver); */

MODULE_DEVICE_TABLE(of, sunxi_eink_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("liuli@allwinnertech.com");
MODULE_DESCRIPTION("Sunxi Eink");
