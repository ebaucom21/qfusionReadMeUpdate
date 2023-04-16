/************************************************************************/
/* WARNING                                                              */
/* define this when we compile for a public release                     */
/* this will protect dangerous and untested pieces of code              */
/************************************************************************/
//#define PUBLIC_BUILD
//#define BROKEN_BUILD

//==============================================
// wsw : jal :	these defines affect every project file. They are
//				work-in-progress stuff which is, sooner or later,
//				going to be removed by keeping or discarding it.
//==============================================

#ifdef BROKEN_BUILD
break
#endif

// pretty solid
#define MOREGRAVITY

// renderer config
//#define CELSHADEDMATERIAL
#define HALFLAMBERTLIGHTING
#define AREAPORTALS_MATRIX

//==============================================
// undecided status

#define TCP_SUPPORT

#define HTTP_SUPPORT

#if defined( HTTP_SUPPORT ) && !defined( TCP_SUPPORT )
#undef HTTP_SUPPORT
#endif

#define DOWNSCALE_ITEMS // Ugly hack for the release. Item models are way too big
#define ELECTROBOLT_TEST

// collaborations
//==============================================
