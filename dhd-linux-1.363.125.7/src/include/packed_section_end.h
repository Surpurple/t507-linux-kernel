/*
 * Declare directives for structure packing. No padding will be provided
 * between the members of packed structures, and therefore, there is no
 * guarantee that structure members will be aligned.
 *
 * Declaring packed structures is compiler specific. In order to handle all
 * cases, packed structures should be delared as:
 *
 * #include <packed_section_start.h>
 *
 * typedef BWL_PRE_PACKED_STRUCT struct foobar_t {
 *    some_struct_members;
 * } BWL_POST_PACKED_STRUCT foobar_t;
 *
 * #include <packed_section_end.h>
 *
 *
 * $ Copyright Open Broadcom Corporation $
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: packed_section_end.h 514727 2014-11-12 03:02:48Z $
 */



#ifdef BWL_PACKED_SECTION
	#undef BWL_PACKED_SECTION
#else
	#error "BWL_PACKED_SECTION is NOT defined!"
#endif





#undef	BWL_PRE_PACKED_STRUCT
#undef	BWL_POST_PACKED_STRUCT
