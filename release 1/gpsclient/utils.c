#include <time.h>
#include <errno.h>

/* Suspend thread in miliseconds precision */
void msleep(int ms)
{
	struct timespec req, rem;

	req.tv_sec = ms / 1000;
	req.tv_nsec = ms % 1000 * 1000000;

	while (nanosleep(&req, &rem) == -1 && errno == EINTR)
		req = rem;
}
