#ifndef DRIVER_RECURSIVE
#  define DRIVER_RECURSIVE
#  include "driver.h"

const struct GameDriver driver_0 = {
	__FILE__, 0, "", 0, 0, 0, 0, 0, 0, 0, 0, NOT_A_DRIVER
};
#  define DRIVER(name, ver) extern struct GameDriver driver_##name##_##ver;
#  define DRIVERNV(name) extern struct GameDriver driver_##name;
#  include "driver.c"
#  undef DRIVER
#  undef DRIVERNV
#  define DRIVER(name, ver) &driver_##name##_##ver,
#  define DRIVERNV(name) &driver_##name,
const struct GameDriver *drivers[] = {
#  include "driver.c"
	0 /* end of array */
};
#if MAMEVER >= 6300
const struct GameDriver *test_drivers[] = { 0 };
#endif
#else /* DRIVER_RECURSIVE */

							 //DRIVER(twd,100)         //S.A.M.: 09/14 Walking Dead, The - V1.0
							 //DRIVER(twd,101)         //S.A.M.: 09/14 Walking Dead, The - V1.01
							 //DRIVER(twd,103)         //S.A.M.: 09/14 Walking Dead, The - V1.03
DRIVER(spagb, 100)
// Because bug in setup doesn't show last driver !
DRIVER(twd, 105)         //S.A.M.: 10/14 Walking Dead, The - V1.05
							 //DRIVER(twd,107)         //S.A.M.: 10/14 Walking Dead, The - V1.07





#endif /* DRIVER_RECURSIVE */
