#include "hptl.h"

hptl_t hptl_get_slow (void);

/******************** VARIABLES ********************/
uint64_t __hptl_time;
uint64_t __hptl_cicles;
static uint64_t __hptl_hz = 0;
static uint64_t __hptl_precision;

#define PRECCISION (__hptl_precision)  // = 100ns
//#define PRECCISION (100000000ull) // = 100ns
// 2^64 =        18.446.744.073.709.551.616
//              36.450.791.397.000.000.000
//                      xx.xxx.xxx.xxx.x00.000.000
//                      18.446.744.073.7 ciclos ~ 76.8s @ 2.4hz
//                       3.647.989.712.700.000.000

/********************* MACROS *********************/
#define overflowflag(isOverflow)           \
	{                                      \
		asm volatile(                      \
		    "pushf ;"                      \
		    "pop %%rax"                    \
		    : "=a"(isOverflow));           \
		isOverflow = (isOverflow & 0x800); \
	}

/*************** STRUCTURES & UNIONS ***************/
typedef union {
	uint64_t tsc_64;
	struct {
		uint32_t lo_32;
		uint32_t hi_32;
	};
} _hptlru;  // hptl rdtsc union.

/*************** PRIVATE FUNCTIONS ***************/
struct timespec hptl_ts_diff (struct timespec start, struct timespec end,
                              char *sign);

/*************** iDPDK FUNCTIONS ***************/
static inline uint64_t hptl_rdtsc (void) {
	_hptlru tsc;

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	return tsc.tsc_64;
}
static int set_tsc_freq_from_clock (void) {
#ifdef CLOCK_MONOTONIC_RAW
#define NS_PER_SEC 1E9
	uint64_t ns, end, start;
#ifdef HPTL_DEBUG
	printf ("[HPTLib] Using CLOCK_MONOTONIC_RAW to obtain CPU Hz...\n");
#endif

	struct timespec sleeptime = {.tv_sec = 0,
	                             .tv_nsec = 500000000}; /* 1/2 second */

	struct timespec t_start, t_end;

	if (clock_gettime (CLOCK_MONOTONIC_RAW, &t_start) == 0) {
		start = hptl_rdtsc ();
		nanosleep (&sleeptime, NULL);
		clock_gettime (CLOCK_MONOTONIC_RAW, &t_end);
		end = hptl_rdtsc ();

		ns = ((t_end.tv_sec - t_start.tv_sec) * NS_PER_SEC);
		ns += (t_end.tv_nsec - t_start.tv_nsec);

		double secs = (double)ns / NS_PER_SEC;
		__hptl_hz   = (uint64_t) ((end - start) / secs);
		return 0;
	}

#endif
	return -1;
}

static void set_tsc_freq_linux (void) {
	char tmp[100];  //"/sys/devices/system/cpu/cpuXXX/cpufreq/cpuinfo_cur_freq";
	volatile int i;
	int status;
	unsigned cpu;
	FILE *f;

#ifdef HPTL_DEBUG
	printf ("[HPTLib] Using Linux to obtain CPU Hz...\n");
#endif

	status = syscall (SYS_getcpu, &cpu, NULL, NULL);

	if (status == -1) {
#ifdef HPTL_DEBUG
		printf ("[HPTLib] ERROR: HPTLib.set_tsc_freq_linux.syscall\n");
#endif
		exit (-1);
	}

	sprintf (tmp, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_cur_freq",
	         cpu);

#ifdef HPTL_DEBUG
	printf ("[HPTLib] Assuming HPTLib is going to run on %d...\n", cpu);
#endif

	f = fopen (tmp, "r");

	if (f == NULL) {
		perror ("HPTLib.set_tsc_freq_linux.fopen");
		exit (-1);
	}

	for (i = 0; i < 15000000; i++)
		;  // warm the CPU

	if (fgets (tmp, sizeof (tmp), f) == NULL) {
		perror ("HPTLib.set_tsc_freq_linux.fgets");
		exit (-1);
	}

	__hptl_hz = atol (tmp) * 1000ul;

	fclose (f);
}

/*
 * This function measures the TSC frequency. It uses a variety of approaches.
 *
 * 1. If kernel provides CLOCK_MONOTONIC_RAW we use that to tune the TSC value
 * 2. If kernel does not provide that, and we have HPET support, tune using HPET
 * 3. Lastly, if neither of the above can be used, just sleep for 1 second and
 * tune off that, printing a warning about inaccuracy of timing
 */
static void set_tsc_freq (void) {
	if (set_tsc_freq_from_clock () < 0) {
		set_tsc_freq_linux ();
	}
}

/********************* FUNCTIONS *********************/

int hptl_init (hptl_config *conf) {
#ifdef HPTL_ONLYCLOCKREALTIME
	hptl_config config;
	int i, k = 1;

#ifdef HPTL_DEBUG
	printf ("[HPTL] INFO: Starting HPTL %s\n", hptl_VERSION);
#endif

	// load config
	if (conf == NULL) {
		config.precision  = 7;
		config.clockspeed = 0;

	} else {
		config = *conf;
	}

	if (config.precision > 9) {
#ifdef HPTL_DEBUG
		printf ("[HPTLib] Error: precision %u>9\n", config.precision);
#endif
		return -1;
	}

	// load clockspeed
	if (config.clockspeed == 0) {
		set_tsc_freq ();

	} else {
		__hptl_hz = config.clockspeed;
	}

	// config precision
	for (i = 0; i < config.precision; i++) {
		k *= 10;
	}

	PRECCISION = k;

	hptl_sync ();

	if (config.clockspeed == 0) {
		hptl_calibrateHz (0);
	}

#ifdef HPTL_DEBUG
	printf ("[HPTLib] Started : Hz:%lu cicles:%lu tof:%lu\n", __hptl_hz,
	        __hptl_cicles, __hptl_time);
#endif
#endif

	return 0; //always return 0
}

void hptl_sync (void) {
	struct timespec tmp;

	if (clock_gettime (CLOCK_REALTIME, &tmp) != 0) {
		printf ("[HPTLib] WARN: Clock_gettime ERROR!\n");
	}

	__hptl_cicles = hptl_rdtsc ();
	__hptl_time =
	    tmp.tv_sec * PRECCISION + tmp.tv_nsec / (1000000000ull / PRECCISION);

#ifdef HPTL_DEBUG
	printf ("[HPTLib] Sync: cicles:%lu tof:%lu\n", __hptl_cicles, __hptl_time);
#endif
}

/*
 * HZ calibration
 * @param oldTime time obtained by hptl_get()
 * @param newTime the newer time obtained by system precision-function
 * @param diffTime the time between executions, example if hptl_get takes 17ns
 * and clockgettime 22ns, 5 (22-17) should be used
 * @return the hz modified
 */
int hptl_calibrateHz (int diffTime) {
#ifdef HPTL_ONLYCLOCKREALTIME
	struct timespec newTime;

	// get the hptltime
	unsigned long long tmp;
	volatile unsigned long long Oflag;

	_hptlru tsc;

	hptl_waitns (750000000);

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	tmp = ((tsc.tsc_64 - __hptl_cicles) * PRECCISION);

	overflowflag (Oflag)

	    if (Oflag) {
		hptl_sync ();
		return hptl_calibrateHz (diffTime);
	}

	// calibrates the time provided by diffTime
	clock_gettime (CLOCK_REALTIME, &newTime);
	newTime.tv_nsec += diffTime;
	int hzCalibrated = 0;

	unsigned long long newhptl;
	struct timespec error, errorPrima;
	char sign, signPrima;

	errorPrima = hptl_ts_diff (hptl_timespec ((tmp / __hptl_hz) + __hptl_time),
	                           newTime, &signPrima);

	do {
		error = errorPrima;
		sign  = signPrima;
		hzCalibrated++;

		newhptl = (tmp / (__hptl_hz + hzCalibrated)) + __hptl_time;
		errorPrima =
		    hptl_ts_diff (hptl_timespec (newhptl), newTime, &signPrima);

		/*printf("\n Was %c %lu s, %3lu ms, %3lu us, %3lu ns ; now %c
		   %lu s, %3lu ms, %3lu us, %3lu ns\n",
		           sign,
		           error.tv_sec,
		           (error.tv_nsec / 1000000000L) % 1000L,
		           (error.tv_nsec / 1000L) % 1000L,
		           error.tv_nsec % 1000L,
		           signPrima,
		           errorPrima.tv_sec,
		           (errorPrima.tv_nsec / 1000000000L) % 1000L,
		           (errorPrima.tv_nsec / 1000L) % 1000L,
		           errorPrima.tv_nsec % 1000L
		            );*/
	} while (errorPrima.tv_nsec <= error.tv_nsec && errorPrima.tv_nsec > 128);

	do {
		error     = errorPrima;
		signPrima = sign;
		hzCalibrated--;

		newhptl = (tmp / (__hptl_hz + hzCalibrated)) + __hptl_time;
		errorPrima =
		    hptl_ts_diff (hptl_timespec (newhptl), newTime, &signPrima);
	} while (errorPrima.tv_nsec <= error.tv_nsec && errorPrima.tv_nsec > 128);

	__hptl_hz += hzCalibrated;
	return hzCalibrated;
#else
	return 0;
#endif
}

hptl_t hptl_get (void) {
	unsigned long long tmp;
	volatile unsigned long long Oflag;

	_hptlru tsc;

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	tmp = ((tsc.tsc_64 - __hptl_cicles) * PRECCISION);

	overflowflag (Oflag)

	    if (Oflag) {
		hptl_sync ();
		return hptl_get ();
	}

	return (tmp / __hptl_hz) + __hptl_time;
}

/**
 * Return the resolution in terms of ns
 **/
uint64_t hptl_getres (void) { return 1000000000ull / PRECCISION; }

/**
 * Wait certain ns actively
 **/
void hptl_waitns (uint64_t ns) {
	hptl_t start, end;

	// start = hptl_rdtsc();
	start = hptl_get ();

	// float cycles = ((float)ns)*(((float)__hptl_hz)/1000000000.);
	// end = start + cycles;
	end = start + ns;

	do {
		// start = hptl_rdtsc();
		start = hptl_get ();
	} while (start < end);
}

/**
 * Converts from realtime format to timespect format
 **/
struct timespec hptl_timespec (hptl_t u64) {
	struct timespec tmp;

	tmp.tv_sec = u64 / PRECCISION;
	tmp.tv_nsec =
	    (u64 - (tmp.tv_sec * PRECCISION)) * (1000000000ull / PRECCISION);

	return tmp;
}

/**
 * Converts from realtime format to timeval format
 **/
struct timeval hptl_timeval (hptl_t u64) {
	struct timeval tmp;

	tmp.tv_sec  = u64 / PRECCISION;
	tmp.tv_usec = ((u64 - (tmp.tv_sec * PRECCISION)) * 1000000ull) / PRECCISION;

	return tmp;
}

/**
 * Converts from HPTLib format to ns from 01 Jan 1970
 **/
uint64_t hptl_ntimestamp (hptl_t hptltime) {
	return hptltime * (1000000000ull / PRECCISION);
}

struct timespec hptl_ts_diff (struct timespec start, struct timespec end,
                              char *sign) {
	struct timespec temp;

	if (start.tv_sec > end.tv_sec) {
		temp  = end;
		end   = start;
		start = temp;

		if (sign != NULL) {
			*sign = '-';
		}
	} else if (start.tv_sec == end.tv_sec && start.tv_nsec > end.tv_nsec) {
		temp  = end;
		end   = start;
		start = temp;

		if (sign != NULL) {
			*sign = '-';
		}
	} else if (sign != NULL) {
		*sign = '+';
	}

	if ((end.tv_nsec - start.tv_nsec) < 0) {
		temp.tv_sec  = end.tv_sec - start.tv_sec - 1;
		temp.tv_nsec = 1000000000ull + end.tv_nsec - start.tv_nsec;
	} else {
		temp.tv_sec  = end.tv_sec - start.tv_sec;
		temp.tv_nsec = end.tv_nsec - start.tv_nsec;
	}

	return temp;
}

/***********
 * Obsolete...
 ***********/
hptl_t hptl_get_slow (void) {
	double tmp;

	union {
		uint64_t tsc_64;
		struct {
			uint32_t lo_32;
			uint32_t hi_32;
		};
	} tsc;

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	tmp = ((tsc.tsc_64 - __hptl_cicles) * (double)PRECCISION);
	return (tmp / __hptl_hz) + __hptl_time;
}
