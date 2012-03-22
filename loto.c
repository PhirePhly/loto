// LoTo (Lock-out Tag-out) - Job lock utility to prevent multiple 
// 	initiations of long jobs
// Kenneth Finnegan, 2012
// kennethfinnegan.blogspot.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// Much of the implementation details for this utility were originally
// based on the public domain and wonderfully documented lockrun: 
// 	http://www.unixwiz.net/tools/lockrun.html
//
// Revision History:
//	2012 03 06:	Initial Revision - Kenneth Finnegan
//	2012 03 08:	Changed wait behavior - added dithering
//	2012 03 21:	Added -t option to wait a limited time for lock

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>

#define STRMATCH(a,b)		(strcmp((a),(b)) == 0)
#define TRY_AND_LOCK(fd)	flock(fd, LOCK_EX | LOCK_NB)
#define UNLOCK(fd)		flock(fd, LOCK_UN)

// These are shorthand locks for common global resources
#define RESOURCE_LOCK_FMT	"/var/lock/loto.%s"
char *resources[] = { "cpu", "net", NULL};

char * findarg(char *option, char **argv, int *index);
void giveupanddie( char *progname, char *message );

int wait_for_lock = 0, waited_for_lock = 0, verbose = 1, timeout = 0; 
char *lock_file = NULL;

int main(int argc, char *argv[]) {
	int i;
	pid_t childpid;
	time_t t_start, t_end;

	int ret_code, lock_fd, child_cmd = argc;


	time(&t_start);

	// Process all arugments before the --
	for (i = 1; argv[i]; i++) {
		// Check for end of loto arguments
		if (STRMATCH(argv[i], "--")) {
			child_cmd = i + 1;
			break;
		}

		// Divide the option field from the flag if they used =
		char *option = strchr(argv[i], '=');
		if (option != NULL) {
			option[0] = '\0';
			option++;
		}

		// Exclusive lock file
		if (STRMATCH(argv[i], "-L")){
			if (lock_file && verbose >= 1) {
				fprintf(stderr, "%s WARNING: Multiple lock files\n",\
						argv[0]);
			}
			lock_file = findarg(option, argv, &i);
		}

		// Wait for the lock
		else if (STRMATCH(argv[i], "-w")){
			wait_for_lock = 1;
		}

		// Be a Chatty Cathy
		else if (STRMATCH(argv[i], "-v")){
			verbose++;
		}

		// Silence is golden
		else if (STRMATCH(argv[i], "-q")) {
			verbose = 0;
		}

		// Wait for the lock, but with a timeout in seconds
		else if (STRMATCH(argv[i], "-t")) {
			wait_for_lock = 1;
			timeout = atoi(findarg(option, argv, &i));
		}

		// We can't make heads or tails of this option
		else {
			fprintf(stderr, \
				"%s ERROR: \"%s\" isn't a valid option\n", \
				argv[0], argv[i]);
			exit(EXIT_FAILURE);
		}

	}

	// Look deep into the implications of what we're done
	if (argv[child_cmd] == NULL) {
		fprintf(stderr, "%s ERROR: missing command after \"--\"\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}
	if (lock_file == NULL) {
		fprintf(stderr, "%s ERROR: missing -L=/path/to/lock_file\n",
				argv[0]);
		exit(EXIT_FAILURE);
	}
	// Is the lock filename one of the common resource macros?
	for (i = 0 ; resources[i] != NULL; i++) {
		if (STRMATCH(lock_file, resources[i])) {
			lock_file = (char *) malloc(FILENAME_MAX);
			if (lock_file == NULL) {
				fprintf(stderr, "%s ERROR: Malloc() failed\n", argv[0]);
				exit(EXIT_FAILURE);
			}
			sprintf(lock_file, RESOURCE_LOCK_FMT, resources[i]);
			if (verbose > 1) {
				printf("%s: Resource lock file %s.\n",
						argv[0], lock_file);
			}
			break;
		}
	}
	if ( (lock_fd = open(lock_file, O_RDONLY|O_CREAT, 0666)) < 0) {
		fprintf(stderr, "%s ERROR: Can't open lock file: %s\n",
				argv[0], lock_file);
		exit(EXIT_FAILURE);
	}
	// If we will be waiting, randomize the dithering
	if (wait_for_lock) {
		// Note we use the term "random" loosely. A better term
		// would be "unique from every other loto pid," which is
		// really all we need anyways.
		srand(getpid());
	}

	while ( TRY_AND_LOCK(lock_fd) ) {
		if (wait_for_lock > 0) { // Keep trying to get this lock
			unsigned int wait_interval = \
					(rand() % wait_for_lock) + wait_for_lock / 2 + 1;
			if (verbose > 1) {
				printf("%s: waiting for lock on %s - sleeping %d secs\n",\
						argv[0], lock_file, wait_interval);
			}
			waited_for_lock += wait_interval;
			waited_for_lock -= sleep(wait_interval);
			wait_for_lock++; // Progressively back off test rate

			if (timeout && waited_for_lock > timeout) { // Time to give up
				char messagebuffer[100];
				sprintf(messagebuffer, "Timeout to gain lock on %s\n", \
						lock_file);
				giveupanddie(argv[0], messagebuffer);
			}
		} else { // Give up right away
			char messagebuffer[100];
			sprintf(messagebuffer, "Failed to gain lock on %s\n", \
					lock_file);
			giveupanddie(argv[0], messagebuffer);
		}
	}

	fflush(stdout);

	// FFFFFFF    OOO    RRRR     K    K   
	// F         O   O   R   RR   K   K    
	// F        O     O  R    RR  K  K     
	// F        O     O  R   RR   K K      
	// FFFF     O     O  RRRR     KK       
	// F        O     O  R RR     K K      
	// F        O     O  R   R    K  K     
	// F         O   O   R    R   K   K    
	// F          OOO    R     R  K    K   
	// Spin off the actual command and wait for it
	childpid = fork();

	if (childpid == 0) { // we are the child
		close(lock_fd);
		// Morph into what the user actually needs to run
		execvp(argv[child_cmd], &argv[child_cmd]);
	} else if (childpid > 0) { // we are the parent
		if (verbose > 1) {
			printf("%s: Waiting for child process %ld\n", \
					argv[0], (long) childpid);
		}

		childpid = waitpid(childpid, &ret_code, 0);

		time(&t_end);
		t_end = t_end - t_start;
		
		if (verbose > 1) {
			printf("%s: Child %ld exited with %d "\
			"(run time: %ld sec, lock wait: %d sec)\n",
					argv[0], (long) childpid, ret_code, \
					t_end - waited_for_lock, waited_for_lock);
		}

		UNLOCK(lock_fd);

	} else { // we are in trouble
		fprintf(stderr, "%s ERROR: Borked the fork\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	exit(ret_code);
} // END main()

// Try to figure out what the user means
// This lets them say -L=file or -L file
char * findarg(char *option, char **argv, int *index) {
	if (option) {
		return option;
	}

	if (argv[*index + 1]) {
		(*index)++;
		return argv[*index];
	}

	fprintf(stderr, "%s ERROR: \"%s\" needs a parameter\n", \
				argv[0], argv[*index]);
	exit(EXIT_FAILURE);
}

void giveupanddie( char *progname, char *message ) {
	if (verbose > 0) {
		fprintf(stderr, "%s: %s\n",\
			progname, message);
		exit(EXIT_FAILURE);
	} else { // Give up quietly if need be
		exit(EXIT_SUCCESS);
	}
}

// Copyright Kenneth Finnegan, 2012.
