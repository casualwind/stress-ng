/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-builtin.h"

#include <sched.h>

static const char option[] = "taskset";

#if defined(HAVE_SCHED_SETAFFINITY)

static cpu_set_t stress_affinity_cpu_set;

/*
 * stress_check_cpu_affinity_range()
 * @max_cpus: maximum cpus allowed, 0..N-1
 * @cpu: cpu number to check
 */
static void stress_check_cpu_affinity_range(
	const int32_t max_cpus,
	const int32_t cpu)
{
	if ((cpu < 0) || ((max_cpus != -1) && (cpu >= max_cpus))) {
		(void)fprintf(stderr, "%s: invalid range, %" PRId32 " is not allowed, "
			"allowed range: 0 to %" PRId32 "\n", option,
			cpu, max_cpus - 1);
		_exit(EXIT_FAILURE);
	}
}

/*
 * stress_parse_cpu()
 * @str: parse string containing decimal CPU number
 *
 * Returns: cpu number, or exits the program on invalid number in str
 */
static int stress_parse_cpu(const char *const str)
{
	int val;

	if (sscanf(str, "%d", &val) != 1) {
		(void)fprintf(stderr, "%s: invalid number '%s'\n", option, str);
		_exit(EXIT_FAILURE);
	}
	return val;
}

/*
 * stress_set_cpu_affinity()
 * @arg: list of CPUs to set affinity to, comma separated
 *
 * Returns: 0 - OK
 */
int stress_set_cpu_affinity(const char *arg)
{
	cpu_set_t set;
	char *str, *ptr, *token;
	const int32_t max_cpus = stress_get_processors_configured();

	CPU_ZERO(&set);

	str = stress_const_optdup(arg);
	if (!str) {
		(void)fprintf(stderr, "out of memory duplicating argument '%s'\n", arg);
		_exit(EXIT_FAILURE);
	}

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		int i, lo, hi;
		char *tmpptr = strstr(token, "-");

		hi = lo = stress_parse_cpu(token);
		if (tmpptr) {
			tmpptr++;
			if (*tmpptr)
				hi = stress_parse_cpu(tmpptr);
			else {
				(void)fprintf(stderr, "%s: expecting number following "
					"'-' in '%s'\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
			if (hi < lo) {
				(void)fprintf(stderr, "%s: invalid range in '%s' "
					"(end value must be larger than "
					"start value\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
		}
		stress_check_cpu_affinity_range(max_cpus, lo);
		stress_check_cpu_affinity_range(max_cpus, hi);

		for (i = lo; i <= hi; i++)
			CPU_SET(i, &set);
	}
	if (sched_setaffinity(getpid(), sizeof(set), &set) < 0) {
		pr_err("%s: cannot set CPU affinity, errno=%d (%s)\n",
			option, errno, strerror(errno));
		free(str);
		_exit(EXIT_FAILURE);
	}
	shim_memcpy(&stress_affinity_cpu_set, &set, sizeof(stress_affinity_cpu_set));

	free(str);
	return 0;
}

/*
 *  stress_change_cpu()
 *	try and change process to a different CPU.
 *	old_cpu: the cpu to change from, -ve = current cpu (that we don't want to use)
 *					 +ve = cpu don't want to ever use
 */
int stress_change_cpu(const stress_args_t *args, const int old_cpu)
{
	int from_cpu;

	cpu_set_t mask;

	/* only change cpu when --change-cpu is enabled */
	if ((g_opt_flags & OPT_FLAGS_CHANGE_CPU) == 0)
		return old_cpu;

	if (CPU_COUNT(&stress_affinity_cpu_set) == 0) {
		if (sched_getaffinity(0, sizeof(mask), &mask) < 0)
			return old_cpu;		/* no dice */
	} else {
		shim_memcpy(&mask, &stress_affinity_cpu_set, sizeof(mask));
	}

	if (old_cpu < 0) {
		from_cpu = (int)stress_get_cpu();
	} else {
		from_cpu = old_cpu;

		/* Try hard not to use the CPU we came from */
		if (CPU_COUNT(&mask) > 1)
			CPU_CLR((int)from_cpu, &mask);
	}

	if (sched_setaffinity(0, sizeof(mask), &mask) >= 0) {
		unsigned int moved_cpu;

		moved_cpu = stress_get_cpu();
		pr_dbg("%s: process [%jd] (child of instance %d on CPU %u moved to CPU %u)\n",
			args->name, (intmax_t)getpid(), args->instance, from_cpu, moved_cpu);
		return (int)moved_cpu;
	}
	return (int)from_cpu;
}

#else
int stress_change_cpu(const stress_args_t *args, const int old_cpu)
{
	(void)args;

	/* no change */
	return old_cpu;
}

int stress_set_cpu_affinity(const char *arg)
{
	(void)arg;

	(void)fprintf(stderr, "%s: setting CPU affinity not supported\n", option);
	_exit(EXIT_FAILURE);
}
#endif
