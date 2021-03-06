/*
 ** pulse.c -- detect lvs or fos failure
 **
 ** Red Hat Clustering Tools 
 ** Copyright 1999 Red Hat, Inc.
 **
 ** Author:  Erik Troan <ewt@redhat.com>
 **
 **
 ** You may freely redistributed or modified this file under the
 ** terms of the GNU General Public License; version 2 or (at your
 ** option), any later version.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with this program; if not, write to the Free Software
 ** Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **
 **
 ** MODIFICATION HISTORY:
 **
 ** 09/xx/1999   1.0 -   Erik Troan <ewt@redhat.com>
 **              1.9     Original release & repairs
 **
 ** 12/19/1999   1.10    Phil "Bryce" Copeland <copeland@redhat.com>
 **                      Minor typecasts and other changes for 64bit.
 **
 ** 01/08/2000   1.11    Phil "Bryce" Copeland <copeland@redhat.com>
 **                      Changed log() to piranha_log()
 ** 
 ** 01/28/2000   1.12 -  Keith Barrett <kbarrett@redhat.com>
 **              1.14    Added header & history. Minor formatting.
 **
 ** 02/07/2000   1.15 -  Keith Barrett <kbarrett@redhat.com>
 **              1.18    send_arp is in /usr/sbin
 **                      ignore tunl device when fetching hardware addrs
 **
 ** 02/15/2000   1.19 -  Keith Barrett <kbarrett@redhat.com>
 **              1.21    Lots of formatting and comments.
 **
 ** 2/23/2000    1.22 -  Keith Barrett <kbarrett@redhat.com>
 **              1.24    Added -v for debug output
 **                      Generic service option passing to nanny
 **
 ** 2/25/2000    1.25 -  Keith Barrett <kbarrett@redhat.com>
 **              1.30    Added call to lvsRelocateFS routine
 **                      If backupActive and no backup defined, clear it
 **                      Don't arp nat device if no nat device to use
 **
 ** 3/1/2000     1.31    Keith Barrett <kbarrett@redhat.com>
 **                      Added failover service logic
 **
 ** 3/2/2000     1.32    Phil "Bryce" Copeland <copeland@redhat.com>
 **                      Added processing of USR signals
 **                      Added config file copy
 **
 ** 3/4/2000     1.33 -  Keith Barrett <kbarrett@redhat.com>
 **              1.39    Fixed child signal problem with heartbeat
 **                      Added ability for fos to "takover" from master
 **                      if not the active node. Proclaimed stable.
 **                      2nd pulse daemon logic commented out in this file.
 **
 ** 3/6/2000     1.40 -  Keith Barrett <kbarrett@redhat.com>
 **              1.43    Replaced processing for USR signals, but disabled
 **                      the logic for this release because it's incomplete.
 **                      Don't run if all services are flagged as inactive.
 **
 ** 3/7/2000     1.44    Keith Barrett <kbarrett@redhat.com>
 **                      Clean up special take control heartbeat
 **                      Fixed lvs child term logic
 **                      Moved signal blocking to dup the old lvs logic
 **
** 6/13/2000    1.45    Keith Barrett <kbarrett@redhat.com>
**                      Fixed flags getting clobbered and screwing up
**                      the take control heartbeat.
**
** 6/14/2000    1.46    Keith Barrett <kbarrett@redhat.com>
**                      Changed "take control" heartbeat logic
**
** 6/18/2000    1.47    Keith Barrett <kbarrett@redhat.com>
**                      Added debugging to heartbeat logic
**
** 7/20/2000    1.48 -  Keith Barrett <kbarrett@redhat.com>
**              1.49    Changed debug mode & statements. Added --version.
**                      Added Bryce's CFGFILE logic.
**
** 8/17/2000    1.50    Lars Kellogg-Stedman <lars@larsshack.org>
**                      Fixed uninitialized size variable in heartbeat
**
** 9/11/2000	1.51	Philip Copeland <copeland@redhat.com>
**			            Fixed least connections to be lc not pcc
**
** 10/17/2000	1.52	Philip Copeland <bryce@redhat.com>
**                      Added extra --version info
**                      Fixed ifconfig up (should be down) for disabling
**                      interfaces
**
** 5/14/2001    1.53    Keith Barrett <kbarrett@redhat.com>
**                      Minor cleanup, added test-start.
**                      Suppress some errors on startup
**
** 5/30/2001    1.54    Keith Barrett <kbarrett@redhat.com>
**                      Corrected netmask impact on ifconfig and send arps.
**                      If starting FOS and no backup defined, force active.
**                      Renamed lvsProcess variable HA_process.
**
** 6/15/2001	1.55	Philip Copeland <bryce@redhat.com>
**			Globals from header file added
**
** 7/2/2001     1.56    Tim Waugh <twaugh@redhat.com>
**                      Heartbeat on dedicated interface
**                      Shared SCSI
**
** 7/30/2004    1.57    Florin Malita <fmalita@glenayre.com>
**                      Fixed /proc/net/dev scan routine - amMaster().
**
*/


#include <stdlib.h>		/* definition of exit() */
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <popt.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>
#include <setjmp.h>
#include <syslog.h>

#include "globals.h"
#include "lvsconfig.h"
#include "util.h"

#define _PROGRAM_VERSION PULSE

#define _(a) (a)
#define N_(a) (a)

#define PULSE_FLAG_ASDAEMON         (1 << 0)
#define PULSE_FLAG_AMMASTER         (1 << 1)
#define PULSE_FLAG_AMACTIVE         (1 << 2)
#define PULSE_FLAG_NORUN            (1 << 3)
#define PULSE_FLAG_FAILPARTNER      (1 << 4)
#define PULSE_FLAG_RESTART_PARTNER  (1 << 5)
#define PULSE_FLAG_ONHOLD           (1 << 6)
#define PULSE_FLAG_SUPPRESS_GI_MSG  (1 << 7)
#define PULSE_FLAG_SUPPRESS_TC_MSG  (1 << 8)
#define PULSE_FLAG_SUPPRESS_ERRORS  (1 << 9)
#define PULSE_FLAG_LINKFAIL         (1 << 10)
#define PULSE_FLAG_CHILDFAIL        (1 << 11)

#define FOS_SERVICES_UP             (1 << 0)
#define FOS_MONITORS_UP             (1 << 1)

#define HEARTBEAT_RUNNING_MAGIC     0xbdaddbda
#define HEARTBEAT_STOPPED_MAGIC     0xadbddabd
#define HEARTBEAT_HOLD_MAGIC        0xDeafBabe
#define HEARTBEAT_HELD_MAGIC        0xDeafDaDa
#define FOS_HEARTBEAT_RUNNING_MAGIC 0xBeefBabe
#define FOS_HEARTBEAT_STOPPED_MAGIC 0xDeadBeef
#define FOS_HEARTBEAT_STOPME_MAGIC  0xDeadDaDa

/* Globals icky :-( */
pid_t piranha;
pid_t pid;
struct sigaction sig;
int gflag = 0;

static char *configFile = (char *) CFGFILE;	/* Supplied by globals.he */
static char *lvsBinary = (char *) "/usr/sbin/lvsd";
static char *fosBinary = (char *) "/usr/sbin/fos";
static int pulse_debug = 0;
static const int scsires_holdtime = 4;	/* seconds; should be configurable */

volatile int termSignal = 0;
volatile int childSignal = 0;
volatile int User1Signal = 0;
volatile int alarmSignal = 0;
volatile pid_t HA_Process = 0;
volatile pid_t SCSI_Process = 0;

int heartbeat_running_magic = (int) HEARTBEAT_RUNNING_MAGIC;
int heartbeat_stopped_magic = (int) HEARTBEAT_STOPPED_MAGIC;

/* These are the states that we can be in when shared SCSI devices
   are being used. */
enum reserve_state
{
	rs_backup,			/* Backup node, heartbeat OK */
	rs_active,			/* Active node, heartbeat OK */
	rs_reserving_dev,		/* Backup node, trying to take over from dead node */
	rs_resetting_bus,		/* Backup node, trying to break stale reservation */
	rs_waiting,			/* Backup node, waiting to receive heartbeat msg */
	rs_preempting			/* Backup node, trying to take over from partially */
		/* dead node (which is still able to reserve device) */
}
fos_reserve_state = rs_backup;

/* Gateway-awareness */
static pid_t ping_process; /* initially zero automatically */
static int ping_retries; /* initially zero automatically */
static char gateway[16]; /* initially zero automatically */
static int network_fault; /* initially zero automatically */

/* prototypes */
void relay (int s);
void house_keeping (int s);
void initsetproctitle (int argc, char **argv, char **envp);
void setproctitle (const char *fmt, ...);
int ip_check_link(struct in_addr set_addr);



	static void
handleTermSignal (int signal)
{
	termSignal = signal;
}

	static void
handleChildDeath (int signal)
{
	childSignal = signal;
	/* This is only possible with fos. */
}

	static void
handleAlarmSignal (int signal)
{
	alarmSignal = signal;
	/* This is only possible with fos. */
}

/*
 ** unblock_signals () -- unblock signals, to allow child to be killed
 */
	static void
unblock_signals (void)
{
	sigset_t sigs;
	signal (SIGTERM, SIG_DFL);
	sigemptyset (&sigs);
	sigaddset (&sigs, SIGINT);
	sigaddset (&sigs, SIGTERM);
	sigaddset (&sigs, SIGHUP);
	sigaddset (&sigs, SIGCHLD);
	sigaddset (&sigs, SIGALRM);
	sigprocmask (SIG_UNBLOCK, &sigs, NULL);
}

/* Gateway-awareness */
	static int
ping_gateway (void)
{
	char *const envp[] = { NULL };
	char *argv[] = { "/bin/ping", "-n", "-c", "1", "-w", "1",
		gateway, NULL };

	if (ping_retries == 0)
		return 1;

	ping_retries--;
	ping_process = fork ();
	if (ping_process == -1)
	{
		perror ("fork");
		return 1;
	}

	if (ping_process == 0)
	{
		/* Child process. */
		int fd;
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
		close (STDERR_FILENO);
		fd = open ("/dev/null", O_RDWR);
		if (fd != STDIN_FILENO)
			dup2 (fd, STDIN_FILENO);
		if (fd != STDOUT_FILENO)
			dup2 (fd, STDOUT_FILENO);
		if (fd != STDERR_FILENO)
			dup2 (fd, STDERR_FILENO);
		unblock_signals ();
		execve ("/bin/ping", argv, envp);
		exit (1);
	}

	return 0;
}

	static int
find_gateway (void)
{
	char *const envp[] = { NULL };
	char *argv[] = { "/sbin/ip", "route", "show", NULL };
	int fildes[2];
	pid_t pid;
	FILE *f;
	char line[500];
	int ret = 1;

	if (pipe (fildes))
	{
		perror ("pipe");
		return 1;
	}

	pid = fork ();
	if (pid == -1)
	{
		perror ("fork");
		close (fildes[0]);
		close (fildes[1]);
		return 1;
	}

	if (pid == 0)
	{
		/* Child process. */
		int fd;
		close (STDIN_FILENO);
		close (STDOUT_FILENO);
		close (STDERR_FILENO);
		close (fildes[0]);
		fd = open ("/dev/null", O_RDWR);
		if (fd != STDIN_FILENO)
			dup2 (fd, STDIN_FILENO);
		if (fildes[1] != STDOUT_FILENO)
			dup2 (fildes[1], STDOUT_FILENO);
		if (fd != STDERR_FILENO)
			dup2 (fd, STDERR_FILENO);
		unblock_signals ();
		execve ("/sbin/ip", argv, envp);
		exit (1);
	}

	/* Parent process. */
	close (fildes[1]);
	f = fdopen (fildes[0], "r");
	while (!feof (f))
	{
		fgets (line, sizeof (line), f);
		if (!strncmp (line, "default via ", 12))
		{
			char *gw = line + 12;
			size_t len;
			gw += strspn (gw, " ");
			len = strspn (gw, "1234567890.");
			if (len > 15)
				/* Not an IP address.  Don't understand this line. */
				continue;

			gw[len] = '\0';
			memcpy (gateway, gw, len + 1);
			ret = 0;
			break;
		}
	}

	fclose (f);
	waitpid (pid, NULL, 0);
	return ret;
}

	static void
bad_connectivity (int flags)
{
	piranha_log (flags, "remote service unavailable; gateway unavailable");
	piranha_log (flags, "I SEEM TO HAVE A NETWORK FAULT!");
	network_fault = 1;
}

/* Check to see if we can reach the router between us and the
 * outside world.  Not synchronous: bad_connectivity() gets called
 * on failure; SIGCHLD signal processing function deals with success. */
	static void
check_connectivity (int flags)
{
	if (find_gateway ())
	{
		bad_connectivity (flags);
		return;
	}

	ping_retries = 3;
	if (ping_gateway ())
		bad_connectivity (flags);
}

static void handleHupSignal(int signal) {
	kill(HA_Process, SIGHUP);
}

#if 0
/************** ### Disable incomplete USR1 logic ### ************
  static void handleUser1Signal(int signal) {
  User1Signal = signal;
  }
 ****************************************************************/
#endif


/*
 **  getInterfaceAddress() -- Get address of a network device
 */

	static int
getInterfaceAddress (int s, char *device, struct in_addr *addr, int flags)
{
	struct ifreq req;
	struct sockaddr_in *addrp;

	strcpy (req.ifr_name, device);

	if (ioctl (s, SIOCGIFADDR, &req))
	{
		if (strncmp ("tunl", device, 4))	/* log only if not tunl dev */
			piranha_log (flags,
					(char *) "SIOCGIFADDR failed: %s", strerror (errno));
		/*
		 ** Note: suppress errors about tunl because it's in /proc even
		 ** if it is not being used, so it will appear here.
		 */

		return 1;
	}

	addrp = (struct sockaddr_in *) &req.ifr_addr;
	memcpy (addr, &addrp->sin_addr, sizeof (*addr));
	return 0;
}





/*
 **  getInterfaceInfo() -- get mac address for validated device
 */

	static void
getInterfaceInfo (int s,
		char *device, struct in_addr *addr, char *hwaddr, int flags)
{

	struct ifreq req;
	struct sockaddr_in *addrp;

	strcpy (req.ifr_name, device);

	if (ioctl (s, SIOCGIFBRDADDR, &req))
	{
		piranha_log (flags,
				(char *) "SIOCGIFBRDADDR failed: %s", strerror (errno));
		return;
	}

	addrp = (struct sockaddr_in *) &req.ifr_addr;
	memcpy (addr, &addrp->sin_addr, sizeof (*addr));

	if (ioctl (s, SIOCGIFHWADDR, &req))
	{
		piranha_log (flags,
				(char *) "SIOCGIFHWADDR failed: %s", strerror (errno));
		return;
	}

	memcpy (hwaddr, req.ifr_hwaddr.sa_data, IFHWADDRLEN);
}





/*
 **  disableInterface() -- Disable ('ifconfig xxx down') an interface
 */

	static void
disableInterface (char *device, int flags)
{

	pid_t child;
	char *ifconfigArgs[5];
	int i = 0;

	ifconfigArgs[i++] = (char *) "/sbin/ifconfig";

	ifconfigArgs[i++] = device;
	ifconfigArgs[i++] = (char *) "down";
	ifconfigArgs[i++] = NULL;
	ifconfigArgs[4] = NULL;

	if (pulse_debug)
		piranha_log (flags,
				(char *)
				"DEBUG -- Executing '/sbin/ifconfig %s down'", device);

	if (!(child = fork ()))
	{
		logArgv (flags, ifconfigArgs);
		execv (ifconfigArgs[0], ifconfigArgs);
		exit (-1);
	}

	waitpid (child, NULL, 0);
}





/*
 **  sendArps() -- send arps for a device, mac address, and ip address combo
 */

	static void
sendArps (char *device,
		struct in_addr *address,
		struct in_addr *vip_nmask, int num, int flags)
{
	int i;
	int s;
	char *floatAddr = strdup (inet_ntoa (*address));
	char *netmask = strdup (inet_ntoa (*vip_nmask));

	struct in_addr bcast;

	char *broadcast = NULL;
	struct in_addr addr;
	char hwaddr[IFHWADDRLEN];
	char hwname[20];
	pid_t child;
	char *ifconfigArgs[10];
	char *ifconfigArgsFull[10];
	char *sendArpArgv[10];
	int netmask_specified = 0;


	sendArpArgv[0] = (char *) "/usr/sbin/send_arp";
	sendArpArgv[1] = (char *) "-i";
	sendArpArgv[2] = device;
	sendArpArgv[3] = floatAddr;
	sendArpArgv[4] = hwname;
	sendArpArgv[5] = broadcast;
	sendArpArgv[6] = (char *) "ffffffffffff";
	sendArpArgv[7] = NULL;

	ifconfigArgs[0] = (char *) "/sbin/ifconfig";
	ifconfigArgs[1] = device;
	ifconfigArgs[2] = floatAddr;
	ifconfigArgs[3] = (char *) "up";
	ifconfigArgs[4] = NULL;

	if (vip_nmask)
	{
		if (vip_nmask->s_addr)
			netmask_specified = -1;
		/* if network mask ptr is OK, AND it points to a non-zero value */
	}


	if (netmask_specified)
	{
		/* A small calculation for to get the broadcast */
		bcast.s_addr =
			(address->s_addr & vip_nmask->s_addr) | ~vip_nmask->s_addr;

		ifconfigArgsFull[0] = (char *) "/sbin/ifconfig";
		ifconfigArgsFull[1] = device;
		ifconfigArgsFull[2] = floatAddr;
		ifconfigArgsFull[3] = (char *) "netmask";
		ifconfigArgsFull[4] = netmask;
		ifconfigArgsFull[5] = (char *) "broadcast";
		ifconfigArgsFull[6] = (char *) inet_ntoa (bcast);
		ifconfigArgsFull[7] = (char *) "up";
		ifconfigArgsFull[8] = NULL;
	}


	if (!(child = fork ()))
	{
		if (netmask_specified)
		{
			logArgv (flags, ifconfigArgsFull);

			if (pulse_debug)
				piranha_log (flags, (char *)
						"DEBUG -- Executing '/sbin/ifconfig %s %s netmask %s up'",
						device, floatAddr, netmask);
			execv (ifconfigArgs[0], ifconfigArgsFull);

		}
		else
		{
			logArgv (flags, ifconfigArgs);

			if (pulse_debug)
				piranha_log (flags, (char *)
						"DEBUG -- Executing '/sbin/ifconfig %s %s up'",
						device, floatAddr);
			execv (ifconfigArgs[0], ifconfigArgs);
		}
		exit (-1);
	}

	waitpid (child, NULL, 0);
	device = strdup (device);

	if (strchr (device, ':'))
		*strchr (device, ':') = '\0';

	sendArpArgv[2] = device;

	s = socket (AF_INET, SOCK_DGRAM, 0);
	getInterfaceInfo (s, device, &addr, hwaddr, flags);
	close (s);

	for (i = 0; i < 6; i++)
	{
		sprintf (hwname + (i * 2), "%02X", ((unsigned int) hwaddr[i]) & 0xFF);
	}

	broadcast = inet_ntoa (addr);

	sendArpArgv[4] = hwname;
	sendArpArgv[5] = broadcast;

	logArgv (flags, sendArpArgv);

	if (pulse_debug)
		piranha_log (flags, (char *) "DEBUG -- Executing '/usr/sbin/send_arp'");

	for (i = 0; i < num; i++)
	{
		if (!(child = fork ()))
		{
			execv (sendArpArgv[0], sendArpArgv);
			exit (1);
		}

		waitpid (child, NULL, 0);
		sleep (1);
	}
}





/*
 **  deactivateFos() --  stop fos process and shut down all VIP interfaces
 */

	static void
deactivateFos (struct lvsConfig *config, int flags, int cleanup)
{

	int i;
	int j;

	if (flags & PULSE_FLAG_NORUN)
		return;

	if (HA_Process > 0)
	{
		kill (HA_Process, SIGTERM);
		waitpid (HA_Process, NULL, 0);
		HA_Process = 0;
	}


	if (cleanup)
	{
		/* deactivate the interfaces */
		for (i = 0; i < config->numFailoverServices; i++)
		{
			if (!config->failoverServices[i].isActive)
				continue;

			for (j = 0; j < i; j++)
			{
				if (!memcmp (&config->failoverServices[i].virtualAddress,
							&config->failoverServices[j].virtualAddress,
							sizeof (config->failoverServices[i].
								virtualAddress)))
					break;
			}

			if (j == i)
				disableInterface (config->failoverServices[i].virtualDevice,
						flags);
		}
	}
}






/*
 **  activateFosServices() -- stop old fos & monitors, start new fos & IP
 **                           services. set up devices and arps.
 **
 */

	static pid_t
activateFosServices (struct lvsConfig *config, int flags)
{

	pid_t child = 0;
	pid_t fake1;
	pid_t *fake = alloca (sizeof (*fake) * (config->numVirtServers + 1));
	int i;
	int j;
	int numFakes;
	char *argv[7];

	argv[0] = fosBinary;
	argv[1] = (char *) "--active";
	argv[2] = (char *) "-c";
	argv[3] = configFile;
	argv[4] = (char *) "--nodaemon";
	argv[5] = NULL;
	argv[6] = NULL;


	if (HA_Process)
		deactivateFos (config, flags, 0);
	/* Shutdown old monitor programs if necessary */

	/* Run fake for 5 seconds on each interface */
	if (!(fake1 = fork ()))
	{
		if (fork ())
		{
			exit (0);
		}

		/* we're init's problem now */

		numFakes = 0;

		for (i = 0; i < config->numFailoverServices; i++)
		{
			if (!config->failoverServices[i].isActive)
				continue;

			for (j = 0; j < i; j++)
			{
				if (!config->failoverServices[j].isActive)
					continue;

				if (!memcmp (&config->failoverServices[i].virtualAddress,
							&config->failoverServices[j].virtualAddress,
							sizeof (config->failoverServices[i].
								virtualAddress)))
					break;
			}

			if (j == i)
			{
				if (!(fake[numFakes++] = fork ()))
				{
					sendArps (config->failoverServices[i].virtualDevice,
							&config->failoverServices[i].virtualAddress,
							&config->failoverServices[i].vip_nmask, 5, flags);
					exit (1);
				}
			}
		}

		for (i = 0; i < numFakes; i++)
			waitpid (fake[i], NULL, 0);

		piranha_log (flags, (char *) "gratuitous fos arps finished");

		exit (0);
	}

	waitpid (fake1, NULL, 0);

	if (flags & PULSE_FLAG_ASDAEMON)
		argv[4] = (char *) "--nofork";	/* Overwrites --nodaemon */

	if (pulse_debug)
		argv[5] = (char *) "-v";	/* Pass along debug mode */

	logArgv (flags, argv);

	if (pulse_debug)
		piranha_log (flags, (char *) "DEBUG -- Executing %s --active", fosBinary);

	if (!(flags & PULSE_FLAG_NORUN))
	{
		if (!(child = fork ()))
		{
			execv (fosBinary, argv);
			exit (-1);
		}
	}

	return child;
}






/*
 **  activateFosMonitors() -- stop old fos & services, start new fos & monitors
 **
 */

	static pid_t
activateFosMonitors (struct lvsConfig *config, int flags)
{

	pid_t child = 0;
	int i, j;
	pid_t *fake = alloca (sizeof (*fake) * (config->numVirtServers + 1));

	char *argv[7];

	argv[0] = fosBinary;
	argv[1] = (char *) "--monitor";
	argv[2] = (char *) "-c";
	argv[3] = configFile;
	argv[4] = (char *) "--nodaemon";
	argv[5] = NULL;
	argv[6] = NULL;

	if (HA_Process)
		(void) deactivateFos (config, flags, 0);
	/* Shut down old services if necessary */

	/* deactivate the interfaces */
	for (i = 0; i < config->numFailoverServices; i++)
	{
		if (!config->failoverServices[i].isActive)
			continue;

		for (j = 0; j < i; j++)
		{
			if (!memcmp (&config->failoverServices[i].virtualAddress,
						&config->failoverServices[j].virtualAddress,
						sizeof (config->failoverServices[i].virtualAddress)))
				break;
		}

		if (j == i)
			disableInterface (config->failoverServices[i].virtualDevice, flags);
	}


	if (flags & PULSE_FLAG_ASDAEMON)
		argv[4] = (char *) "--nofork";	/* Overwrites --nodaemon */

	if (pulse_debug)
		argv[5] = (char *) "-v";	/* Pass along debug mode */

	logArgv (flags, argv);

	if (pulse_debug)
		piranha_log (flags, (char *) "DEBUG -- Executing %s --monitor",
				fosBinary);

	if (!(flags & PULSE_FLAG_NORUN))
	{
		if (!(child = fork ()))
		{
			execv (fosBinary, argv);
			exit (-1);
		}
	}

	return child;
}

/*
   SHARED SCSI ROUTINES START HERE
   */

/*
 ** shared_device() -- return the name of our shared device
 */
	static char *
shared_device (struct lvsConfig *config, int flags)
{
	return ((flags & PULSE_FLAG_AMMASTER) ?
			config->primaryShared : config->backupShared);

}


/*
 **  reserveStop() -- stop tool if running, without triggering any further
 **                   actions
 **
 */
	static void
reserveStop (struct lvsConfig *config, int flags)
{
	pid_t child = 0;
	char *argv[] = { config->reserve, "-d", shared_device (config, flags),
		"--release", NULL };

	fos_reserve_state = rs_backup;
	if (!SCSI_Process)
		return;

	piranha_log (flags, "Stopping SCSI reservation tool");
	kill (SCSI_Process, SIGTERM);
	waitpid (SCSI_Process, NULL, 0);
	SCSI_Process = 0;

	piranha_log (flags, "Releasing SCSI device");

	if (pulse_debug)
		piranha_log (flags, "DEBUG -- Executing %s -d %s --release",
				argv[0], argv[2]);

	if (!(child = fork ()))
	{
		/* Unblock SIGTERM and reset its handler, so that we can be
		   killed if necessary. */
		unblock_signals ();
		execv (argv[0], argv);
		piranha_log (flags, "Couldn't execute \"%s\": %s", argv[0],
				strerror (errno));
		exit (-1);
	}

	waitpid (child, NULL, 0);
}


/*
 **  using_shared_SCSI() -- are we using shared SCSI?
 **
 */
	static int
using_shared_SCSI (struct lvsConfig *config, int flags)
{
	return (config->primaryShared && *config->primaryShared &&
			config->backupShared && *config->backupShared);
}

/*
 ** delay_parameter() -- helper function for scsi_reserve invocations
 **
 **/
	static char *
delay_parameter (struct lvsConfig *config, int flags)
{
	static char *delparm;

	if (!delparm)
	{
		delparm = malloc (50);
		sprintf (delparm, "--delay=%d", scsires_holdtime);
	}

	return delparm;
}

/*
 **  reserveHold() -- stop tool if running, then start it
 **
 */

	static pid_t
reserveHold (struct lvsConfig *config, int flags)
{
	pid_t child = 0;
	char *argv[] = { config->reserve, "-d", shared_device (config, flags),
		"--reserve", "--hold", "--stonith",
		delay_parameter (config, flags), NULL };

	if (!using_shared_SCSI (config, flags))
		return 0;

	if (SCSI_Process)
	{
		/* Shouldn't really get here I guess. */
		reserveStop (config, flags);
	}

	piranha_log (flags, "Starting SCSI reservation tool");

	if (pulse_debug)
		piranha_log (flags, "DEBUG -- Executing "
				"%s -d %d --reserve --hold --stonith %s",
				argv[0], argv[2], argv[6]);

	if (!(child = fork ()))
	{
		/* Unblock SIGTERM and reset its handler, so that we can be
		   killed if necessary. */
		unblock_signals ();
		execv (argv[0], argv);
		piranha_log (flags, "Couldn't execute \"%s\": %s", argv[0],
				strerror (errno));
		exit (-1);
	}

	alarm (scsires_holdtime + 1);

	return child;
}



/*
 **  reserveResetHold() -- reset the SCSI bus, wait, and reissue a reservation
 **
 **  Actually what we do here is run the SCSI reservation utility in a new
 **  process and start a timer: if the timer fires, we know that the SCSI
 **  reservation utility is having success, since it hasn't exited.
 */

	static pid_t
reserveResetHold (struct lvsConfig *config, int flags)
{
	pid_t child = 0;
	char *argv[] = { config->reserve, "-d", shared_device (config, flags),
		"--reset", "--reserve", "--hold", "--stonith",
		delay_parameter (config, flags), NULL };

	if (!using_shared_SCSI (config, flags))
		return 0;

	if (SCSI_Process)
	{
		/* Shouldn't really get here I guess. */
		reserveStop (config, flags);
	}

	piranha_log (flags, "Starting SCSI reservation tool (resetting bus)");

	if (pulse_debug)
		piranha_log (flags, "DEBUG -- Executing "
				"%s -d %s --reset --reserve --hold --stonith %s",
				argv[0], argv[2], argv[7]);

	fos_reserve_state = rs_resetting_bus;
	if (!(child = fork ()))
	{
		/* Unblock SIGTERM and reset its handler, so that we can be
		   killed if necessary. */
		unblock_signals ();
		execv (argv[0], argv);
		piranha_log (flags, "Couldn't execute \"%s\": %s", argv[0],
				strerror (errno));
		exit (-1);
	}

	/* The timer needs to be at least 4 seconds because that's how long
	   the SCSI reservation tool waits in between the reset and the
	   reservation issue. */
	alarm (4 + scsires_holdtime + 1);

	return child;
}



/*
 ** reservePreemptHold() -- preempt the other node and start the timer
 **
 ** We do this by running the SCSI reservation tool with the --preempt
 ** option.  We know if it failed, because it will exit.  But the only
 ** way to know that it succeeded is if it doesn't exit within a
 ** certain amount of time.
 */
	static pid_t
reservePreemptHold (struct lvsConfig *config, int flags)
{
	pid_t child = 0;
	char *argv[] = { config->reserve, "-d", shared_device (config, flags),
		"--preempt", "--hold", "--stonith",
		delay_parameter (config, flags), NULL };

	if (!using_shared_SCSI (config, flags))
		return 0;

	if (SCSI_Process)
	{
		/* Shouldn't really get here I guess. */
		reserveStop (config, flags);
	}

	piranha_log (flags, "Starting SCSI reservation tool (preempting)");

	if (pulse_debug)
		piranha_log (flags, "DEBUG -- Executing "
				"%s -d %s --preempt --hold --stonith %s",
				argv[0], argv[2], argv[6]);

	fos_reserve_state = rs_preempting;
	if (!(child = fork ()))
	{
		/* Unblock SIGTERM and reset its handler, so that we can be
		   killed if necessary. */
		unblock_signals ();
		execv (argv[0], argv);
		piranha_log (flags, "Couldn't execute \"%s\": %s", argv[0],
				strerror (errno));
		exit (-1);
	}

	alarm (scsires_holdtime + 1);

	return child;
}

/*
 ** resetSelf() -- reset, without touching shared SCSI device
 */
	static void
resetSelf (int flags)
{
	int confidence = 1;
	for (;;)
	{
		char *argv[3];
		int i = 0;
		argv[i++] = "/sbin/reboot";
		argv[i++] = "-df";
		argv[i++] = NULL;
		execv (argv[0], argv);
		if (confidence)
			piranha_log (flags, "Couldn't execute \"%s\": %s", argv[0],
					strerror (errno));
		confidence = 0;
	}
}

/*
   SHARED SCSI ROUTINES END HERE
   */





/*
 ** syncDaemon() -- (re)start IPVS synchronisation daemon
 */

	static void
syncDaemon (struct lvsConfig *config, int flags, int master)
{

	pid_t child;
	char *ipvsadmArgs[8];
	char id[4];

	if (config->useSyncDaemon == 0)
		return;

	ipvsadmArgs[0] = (char *) config->vsadm;
	ipvsadmArgs[1] = (char *) "--stop-daemon";
	ipvsadmArgs[2] = (char *) "backup";
	ipvsadmArgs[3] = NULL;

	if (pulse_debug)
		piranha_log (flags,
				(char *)
				"DEBUG -- Executing 'ipvsadm --stop-daemon %s'", ipvsadmArgs[2]);

	if (!(child = fork ()))
	{
		logArgv (flags, ipvsadmArgs);
		execv (ipvsadmArgs[0], ipvsadmArgs);
		exit (-1);
	}

	waitpid (child, NULL, 0);

	ipvsadmArgs[0] = (char *) config->vsadm;
	ipvsadmArgs[1] = (char *) "--stop-daemon";
	ipvsadmArgs[2] = (char *) "master";
	ipvsadmArgs[3] = NULL;

	if (pulse_debug)
		piranha_log (flags,
				(char *)
				"DEBUG -- Executing 'ipvsadm --stop-daemon %s'", ipvsadmArgs[2]);

	if (!(child = fork ()))
	{
		logArgv (flags, ipvsadmArgs);
		execv (ipvsadmArgs[0], ipvsadmArgs);
		exit (-1);
	}

	waitpid (child, NULL, 0);

	sprintf (id, "%d", (config->syncdID & 0xff));

	ipvsadmArgs[0] = (char *) config->vsadm;
	ipvsadmArgs[1] = (char *) "--start-daemon";
	ipvsadmArgs[2] = (char *) ((master) ? "master" : "backup");
	ipvsadmArgs[3] = (char *) "--mcast-interface";
	ipvsadmArgs[4] = (char *) config->syncdInterface;
	ipvsadmArgs[5] = (char *) "--syncid";
	ipvsadmArgs[6] = (char *) id;
	ipvsadmArgs[7] = NULL;

	if (pulse_debug)
		piranha_log (flags,
				(char *)
				"DEBUG -- Executing 'ipvsadm --start-daemon %s --mcast-interface %s --syncid %s'",
				ipvsadmArgs[2], ipvsadmArgs[4], ipvsadmArgs[6]);

	if (!(child = fork ()))
	{
		logArgv (flags, ipvsadmArgs);
		execv (ipvsadmArgs[0], ipvsadmArgs);
		exit (-1);
	}

	waitpid (child, NULL, 0);
}





/*
 **  sendLvsArps() -- send arps for all VIPs
 */

	static void
sendLvsArps (struct lvsConfig *config, int flags)
{
	pid_t fake1;
	pid_t *fake = alloca (sizeof (*fake) * (config->numVirtServers + 1));
	int i;
	int j;
	int numFakes;

	/* Run fake for 5 seconds on each interface */
	if (!(fake1 = fork ()))
	{
		if (fork ())
		{
			exit (0);
		}

		/* we're init's problem now */

		numFakes = 0;

		if (!(fake[numFakes++] = fork ()))
		{
			if ((config->redirectType == LVS_REDIRECT_NAT) &&
					(config->natRouterDevice != NULL))
				sendArps (config->natRouterDevice,
						&config->natRouter, &config->nat_nmask, 5, flags);
			/* send arps to nat router (if any) */
			exit (1);
		}


		for (i = 0; i < config->numVirtServers; i++)
		{
			if (!config->virtServers[i].isActive)
				continue;

			if (config->virtServers[i].failover_service)
			{
				piranha_log (flags,
						(char *) "Warning; skipping failover service");
				continue;		/* This should not be possible anymore */
			}

			for (j = 0; j < i; j++)
			{
				if (!config->virtServers[j].isActive)
					continue;

				if (!memcmp (&config->virtServers[i].virtualAddress,
							&config->virtServers[j].virtualAddress,
							sizeof (config->virtServers[i].virtualAddress)))
					break;
			}

			if (j == i)
			{
				if (!(fake[numFakes++] = fork ()))
				{
					sendArps (config->virtServers[i].virtualDevice,
							&config->virtServers[i].virtualAddress,
							&config->virtServers[i].vip_nmask, 5, flags);
					exit (1);
				}
			}
		}

		for (i = 0; i < numFakes; i++)
			waitpid (fake[i], NULL, 0);

		piranha_log (flags, (char *) "gratuitous lvs arps finished");

		exit (0);
	}

	waitpid (fake1, NULL, 0);
}





/*
 **  executeCommand() -- 
 */

	static void
executeCommand (struct lvsConfig *config, int flags, char *cmd)
{

	pid_t child = 0;
	char *argv[40];
	char **argp;
	char *token;
	char *command;


	if (!cmd)
		return;

	command = strdup (cmd);

	if (!(flags & PULSE_FLAG_NORUN))
	{
		if (pulse_debug)
			piranha_log (flags,
					(char *)
					"DEBUG -- Executing '%s'", command);

		if (!(child = fork ()))
		{
			argp = argv;
			token = strtok (command, " ");
			while (token != NULL)
			{
				*argp++ = strdup (token);
				token = strtok (NULL, " ");
			}
			*argp++ = NULL;

			logArgv (flags, argv);
			execv (argv[0], argv);
			exit (-1);
		}

		//waitpid (child, NULL, 0);
	}

	free (command);
}





/*
 **  activateLvs() -- Start LVS program, then send arps for all VIPs
 */

	static pid_t
activateLvs (struct lvsConfig *config, int flags)
{

	pid_t child = 0;
	char *argv[6];


	executeCommand (config, flags, config->activeCommand);

	argv[0] = lvsBinary;
	argv[1] = (char *) "--nodaemon";
	argv[2] = (char *) "-c";
	argv[3] = configFile;
	argv[4] = NULL;
	argv[5] = NULL;

	if (flags & PULSE_FLAG_ASDAEMON)
		argv[1] = (char *) "--nofork";

	if (pulse_debug)
		argv[4] = (char *) "-v";

	if (pulse_debug)
		logArgv (flags, argv);

	if (!(flags & PULSE_FLAG_NORUN))
	{
		if (!(child = fork ()))
		{
			execv (lvsBinary, argv);
			exit (-1);
		}
	}

	syncDaemon (config, flags, 1);
	sendLvsArps (config, flags);

	return child;
}





/*
 **  deactiveLVS() --  stop LVS process and shut down all VIP interfaces
 */

	static void
deactivateLvs (struct lvsConfig *config, int flags)
{

	int i, j;


	executeCommand (config, flags, config->inactiveCommand);

	syncDaemon (config, flags, 0);

	if ((flags & PULSE_FLAG_NORUN) || !(flags & PULSE_FLAG_AMACTIVE))
		return;

	kill (HA_Process, SIGTERM);

	/* XXX need a timeout here, which means an alarm, which is gross */
	waitpid (HA_Process, NULL, 0);

	HA_Process = 0;

	/* deactivate the interfaces */
	for (i = 0; i < config->numVirtServers; i++)
	{
		if (!config->virtServers[i].isActive)
			continue;

		if (config->virtServers[i].failover_service)
		{
			piranha_log (flags, (char *) "Warning; skipping failover service");
			continue;		/* This should not be possbile anymore */
		}

		for (j = 0; j < i; j++)
		{
			if (!config->virtServers[j].isActive)
				continue;

			if (!memcmp (&config->virtServers[i].virtualAddress,
						&config->virtServers[j].virtualAddress,
						sizeof (config->virtServers[i].virtualAddress)))
				break;
		}

		if (j == i)
			disableInterface (config->virtServers[i].virtualDevice, flags);
	}

	if ((config->redirectType == LVS_REDIRECT_NAT) &&
			(config->natRouterDevice != NULL))
		disableInterface (config->natRouterDevice, flags);
}





/*
 **  createSocket() -- Create heartbeat socket
 */

	static int
createSocket (struct in_addr *addr, int port, int flags)
{

	int sock;
	long sock_flags;
	struct sockaddr_in address;


	if ((sock = socket (PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		piranha_log (flags, (char *) "failed to create UDP socket: %s",
				strerror (errno));
		return 1;
	}

	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	memcpy (&address.sin_addr, addr, sizeof (*addr));

	if (bind (sock, (struct sockaddr *) &address, sizeof (address)))
	{
		piranha_log (flags, (char *) "failed to bind to heartbeat address: %s",
				strerror (errno));
		return -1;
	}

	/* Set close on exec bit so children don't have the port bound */
	sock_flags = fcntl(sock, F_GETFD);
	sock_flags |= FD_CLOEXEC;
	fcntl(sock, F_SETFD, sock_flags);

	return sock;
}






	void
processChildTerm (struct lvsConfig *config, int *flags)
{
	static int preempt_attempts;
	pid_t child = 0;
	int status;

	while ((child = waitpid (-1, &status, WNOHANG)) > 0)
	{
		if (child == SCSI_Process)
		{
			int s;
			SCSI_Process = 0;

			/* Cancel any timers that were set. */
			alarm (0);

			if (!WIFEXITED (status))
			{
				piranha_log (*flags,
						"reservation process exited abnormally; "
						"treating as failure");
				s = 1;
			}
			else
				s = WEXITSTATUS (status);

			if (fos_reserve_state == rs_active)
			{
				/* Case 1: We were active, and holding a SCSI device, but
				   something has gone wrong.  Check exit status. */

				*flags &= ~PULSE_FLAG_AMACTIVE;

				if (s)
				{
					/* Case 1a: The reservation tool failed.
					   Ugh.  Now what? */
					piranha_log (*flags,
							"reservation process couldn't get initial "
							"reservation.  Config error?");

					/* Really.  Now what? */
				}
				else
				{
					/* Case 1b: The SCSI reservation utility has exited
					   successfully, and so the other node is active now.
					   We have to reset and not write any further data to
					   the shared device. */
					resetSelf (*flags);
				}
			}
			else if (fos_reserve_state == rs_reserving_dev)
			{
				/* Case 2: We are the backup node, trying to go active.
				   We've just tried reserving the device, but that failed. */
				piranha_log (*flags, "failed to reserve device; "
						"resetting SCSI bus and trying again");
				SCSI_Process = reserveResetHold (config, *flags);
				/* Timer is now set. */
			}
			else if (fos_reserve_state == rs_resetting_bus)
			{
				/* Case 3: We are trying to reset the bus, to grab the
				   device because the other node was likely dead, but
				   it turns out that it can still make SCSI
				   reservations. */
				piranha_log (*flags, "reservation conflict: "
						"couldn't grab SCSI device!");
				if (!strcmp (config->reservation_conflict_action, "nothing"))
				{
					/* Case 3a: Do nothing. */
					piranha_log (*flags, "taking no action");
					fos_reserve_state = rs_waiting;
					*flags &= (~(PULSE_FLAG_SUPPRESS_TC_MSG |
								PULSE_FLAG_FAILPARTNER | PULSE_FLAG_AMACTIVE));
				}
				else if (!strcmp (config->reservation_conflict_action,
							"preempt"))
				{
					/* Case 3b: Preempt the other node. */
					piranha_log (*flags, "preempting the other node");
					fos_reserve_state = rs_preempting;
					preempt_attempts = 10;
					SCSI_Process = reservePreemptHold (config, *flags);
				}
			}
			else if (fos_reserve_state == rs_preempting)
			{
				/* Case 3b, continued: We have tried to
				   preempt the other node, but it hasn't worked. */
				piranha_log (*flags, "failed to preempt other node");
				/* Hmm.  Now what? */

				/* Let's try again. */
				if (!--preempt_attempts)
				{
					piranha_log (*flags, "failed to preempt the other "
							"node; giving up");
					fos_reserve_state = rs_waiting;
					*flags &= (~(PULSE_FLAG_SUPPRESS_TC_MSG |
								PULSE_FLAG_FAILPARTNER | PULSE_FLAG_AMACTIVE));
				}
				else
				{
					SCSI_Process = reservePreemptHold (config, *flags);
				}

			}
			else if (fos_reserve_state != rs_backup)
			{
				piranha_log (*flags, "unknown reserve state %d",
						fos_reserve_state);
			}

		}
		else if (child == HA_Process)
		{
			HA_Process = 0;

			/* The failover service monitor has exited. */
			if (*flags & PULSE_FLAG_AMACTIVE)
			{
				piranha_log (*flags,
						(char *) "fos process exited -- restarting it");
				HA_Process = activateFosServices (config, *flags);
			}
			else
			{
				/* We can't see the service on the other node.  There are
				 * two possibilities we will consider:
				 *
				 * 1. The other node has a network fault.
				 * 2. We have a network fault.
				 *
				 * We decide between them based on whether or not we can
				 * ping the default gateway.
				 */
				piranha_log (*flags, "fos process exited -- "
						"checking network connectivity");
				check_connectivity (*flags);
				/* We've fired off a ping.  See case child==ping_process for
				 * the next part of the story.. */
			}
		}
		else if (child == ping_process)
		{
			/* We are in the process of checking connectivity to the
			 * gateway. */
			if (WIFEXITED (status) && !WEXITSTATUS (status))
			{
				/* Successful ping. */
				piranha_log (*flags, "remote service unavailable but we can "
						"see the gateway");
				piranha_log (*flags, "seems like a remote network fault; "
						"performing service failover");

				*flags |= PULSE_FLAG_FAILPARTNER;

				/*
				 ** In a tie, master wins. But in this failure case,
				 ** we want to win the tie this time!
				 */

				if (using_shared_SCSI (config, *flags))
				{
					/* We aren't actually active yet, but we'll tell our
					   partner that we are until we know that we can't be
					   (i.e. until we fail to reserve the SCSI device). */
					*flags |= PULSE_FLAG_AMACTIVE;

					piranha_log (*flags, "Trying to get shared SCSI device");
					fos_reserve_state = rs_reserving_dev;
					SCSI_Process = reserveHold (config, *flags);
					/* Timer is now set. */
				}
				else
				{
					*flags |= PULSE_FLAG_AMACTIVE;
					HA_Process = activateFosServices (config, *flags);
				}
			}
			else if (ping_gateway ())
				/* We tried pinging the gateway a few times, and got
				 * nothing back.  Looks like we're cut off from the world. */
				bad_connectivity (*flags);
		}
		else
			piranha_log (*flags,
					(char *) "Skipping death of unknown child %d", child);
	}
}






/*
 ** processSignals() -- Process any signals that have arrived
 */

	static int
processSignals (struct lvsConfig *config, int *flags, int use_fos,
		sigset_t * sigs)
{
	pid_t child = 0;
	int status;

	if ((childSignal != 0) && (use_fos != 0))
	{
		sigprocmask (SIG_BLOCK, sigs, NULL);
		signal (SIGCHLD, handleChildDeath);
		processChildTerm (config, flags);
		childSignal = 0;
		sigprocmask (SIG_UNBLOCK, sigs, NULL);
	}

	if ((alarmSignal != 0) && (use_fos != 0))
	{
		sigprocmask (SIG_BLOCK, sigs, NULL);
		signal (SIGALRM, handleAlarmSignal);
		alarmSignal = 0;
		sigprocmask (SIG_UNBLOCK, sigs, NULL);

		/* We have reserved the shared SCSI device.  Now we want to
		   become the active node. */
		piranha_log (*flags, "We have the shared SCSI device now");
		*flags |= PULSE_FLAG_AMACTIVE;
		fos_reserve_state = rs_active;
		HA_Process = activateFosServices (config, *flags);
		*flags &= (~(PULSE_FLAG_SUPPRESS_TC_MSG | PULSE_FLAG_FAILPARTNER));
	}

	if ((childSignal != 0) && (use_fos ==0))
	{
		sigprocmask (SIG_BLOCK, sigs, NULL);
		signal (SIGCHLD, handleChildDeath);

		if ((child = waitpid (-1, &status, WNOHANG)) > 0)
		{
			if (child == HA_Process)
			{
				if (WIFEXITED (status))
				{
					piranha_log (*flags, "Child process %d exited with status %d",
							child, WEXITSTATUS (status));
				}
				if (WIFSIGNALED (status))
				{
					piranha_log (*flags, "Child process %d terminated with signal %d",
							child, WTERMSIG (status));
				}

				deactivateLvs (config, *flags);
				*flags &= (~PULSE_FLAG_AMACTIVE);
				*flags |= (PULSE_FLAG_CHILDFAIL);
			}
			else
			{
				piranha_log (*flags, "Skipping death of unknown child %d", child);
			}
		}

		childSignal = 0;
		sigprocmask (SIG_UNBLOCK, sigs, NULL);
	}

	if (termSignal != 0)
	{
		piranha_log (*flags, "Terminating due to signal %d", termSignal);

		if (use_fos)
		{
			reserveStop (config, *flags);
			deactivateFos (config, *flags, -1);
		}
		else
		{
			deactivateLvs (config, *flags);
		}

		return 1;
	}

	return 0;
}


	int
check_links(struct lvsConfig *config, int *flags)
{
	int ret = 1;

	if (!config->monitorLinks)
		return 1; /* If we're not monitoring links, always return true */

	if (!config->backupActive)
		return 1; /* If we're monitoring links but no backup, disable...
			     us being quasi-up is better than nothing up at all */

	/* Ok check the links */
	if (*flags & PULSE_FLAG_AMMASTER)
	{
		if (!ip_check_link(config->primaryServer))
		{
			if (!(*flags & PULSE_FLAG_LINKFAIL))
				piranha_log(*flags, "Primary NIC: link down");
			ret = 0;
		}
		if (!ip_check_link(config->primaryPrivate))
		{
			if (!(*flags & PULSE_FLAG_LINKFAIL))
				piranha_log(*flags, "Private NIC: link down");
			ret = 0;
		}

		return ret;
	}

	/* We're the backup, check the backup server links */
	if (!ip_check_link(config->backupServer))
	{
		if (!(*flags & PULSE_FLAG_LINKFAIL))
			piranha_log(*flags, "Primary NIC: link down");
		ret = 0;
	}
	if (!ip_check_link(config->backupPrivate))
	{
		if (!(*flags & PULSE_FLAG_LINKFAIL))
			piranha_log(*flags, "Private NIC: link down");
		ret = 0;
	}

	return ret;
}


	void
send_heartbeat (struct lvsConfig *config, int sock, int sock_priv, int *flags,
		struct sockaddr_in *partner, struct sockaddr_in *partner_priv)
{
	unsigned int magic = heartbeat_stopped_magic;
	int rc;

	/* If there's no backup server specified, then there's nothing to do. */
	if (!config->backupActive)
		return;

	if (*flags & PULSE_FLAG_AMACTIVE)
		magic = heartbeat_running_magic;

	if (*flags & PULSE_FLAG_FAILPARTNER)
	{
		if ((*flags & PULSE_FLAG_SUPPRESS_TC_MSG) == 0)
		{
			piranha_log (*flags, (char *)
					"Notifying partner WE are taking control!");
			*flags |= PULSE_FLAG_SUPPRESS_TC_MSG;
		}

		magic = FOS_HEARTBEAT_STOPME_MAGIC;
		/* Special case where fos forces partner to go inactive */
	}

	if (pulse_debug)
		piranha_log (*flags, (char *) "DEBUG -- Sending heartbeat...");

	/* Send to partner's IP address over public interface */
	rc = sendto (sock, &magic, sizeof (magic), (int) 0,
			(struct sockaddr *) partner,
			(socklen_t) sizeof (*partner));

	if ((rc == -1) && (pulse_debug != 0))
	{
		piranha_log (*flags, (char *)
				"Warning -- failed to send heartbeat (public if): %s",
				strerror (errno));
	}

	if (sock_priv == -1 || !partner_priv)
		return;

	/* Send to partner's IP address over private interface */
	rc = sendto (sock_priv, &magic, sizeof (magic), (int) 0,
			(struct sockaddr *) partner_priv,
			(socklen_t) sizeof (*partner_priv));
	if ((rc == -1) && (pulse_debug != 0))
	{
		piranha_log (*flags, "Warning -- failed to send "
				"heartbeat (private if): %s",
				strerror (errno));
	}
}


/*
   Pull this out so we can shutdown without duplicating the 
   block of code...
   */
#define shutdown_services(msg) \
	do { \
		if (use_fos) \
		{ \
			/* Failover Services */ \
			if (flags & PULSE_FLAG_FAILPARTNER) \
			{ \
				piranha_log (flags, (char *) msg ": we want control!"); \
			} \
			else \
			{ \
				piranha_log (flags,(char *)msg ": deactivating FOS"); \
				reserveStop (config, flags); \
				HA_Process = activateFosMonitors (config, flags); \
				flags &= (~PULSE_FLAG_AMACTIVE); \
			} \
			/* else ignore it and tell him to go */ \
		} \
		else \
		{ \
			/* Virtual Services */ \
			piranha_log (flags, (char *)msg ": deactivating LVS"); \
			deactivateLvs (config, flags); \
			flags &= (~PULSE_FLAG_AMACTIVE); \
		} \
	} while(0)

#define startup_services(msg) \
	do { \
		if (use_fos) \
		{ \
			if (using_shared_SCSI (config, flags)) \
			{ \
				if (fos_reserve_state == rs_backup) \
				{ \
					piranha_log (flags, (char *)msg \
							": activating fos"); \
					fos_reserve_state = rs_reserving_dev; \
					SCSI_Process = reserveHold \
					(config, flags); \
					/* Timer is now set. */ \
				} \
			} \
			else \
			{ \
				piranha_log (flags, (char *) msg \
						": activating fos"); \
				flags |= PULSE_FLAG_AMACTIVE; \
				HA_Process = activateFosServices \
				(config, flags); \
			} \
		} \
		else \
		{ \
			piranha_log (flags, \
					(char *) msg ": activating lvs"); \
			HA_Process = activateLvs (config, flags); \
			flags |= PULSE_FLAG_AMACTIVE; \
		} \
	} while (0)


/*
 **  run() -- main login routine
 */

	static int
run (struct lvsConfig *config, int sock, int sock_priv, int flags)
{

	struct timeval now;
	struct timeval sendHeartBeat;
	struct timeval needHeartBeat;
	struct timeval timeout;
	struct timeval *smaller;
	fd_set fdset;
	int done = 0;
	int rc = 0;
	unsigned int magic;		/* beware of sign change during assignment */
	socklen_t size;		/* See page 27 of R.Stevens UNIX Net. prog. vol1 ed 2 */
	struct sockaddr_in partner, partner_priv;
	struct sockaddr_in sender;
	int use_fos = 0;
	sigset_t sigs;


	if (config->lvsServiceType == LVS_SERVICE_TYPE_FOS)
		use_fos = -1;

	sigemptyset (&sigs);
	sigaddset (&sigs, SIGINT);
	sigaddset (&sigs, SIGTERM);
	sigaddset (&sigs, SIGHUP);
	sigaddset (&sigs, SIGCHLD);
	sigaddset (&sigs, SIGALRM);

	/***** ### disable incomplete processing of USR 1 & 2 ###
	  sigaddset(&sigs, SIGUSR1);
	  sigaddset(&sigs, SIGUSR2);
	 ********************************************************/

	sigprocmask (SIG_BLOCK, &sigs, NULL);

	signal (SIGINT, handleTermSignal);
	signal (SIGTERM, handleTermSignal);
	signal (SIGALRM, handleAlarmSignal);
	signal (SIGHUP, handleHupSignal);
	signal (SIGCHLD, handleChildDeath);

	/***** ### disable incomplete processing of USR 1 & 2 ###
	  signal(SIGUSR1, handleUser1Signal);
	 ********************************************************/

	if (config->lvsServiceType == LVS_SERVICE_TYPE_FOS)
	{
		use_fos = -1;
		heartbeat_running_magic = (int) FOS_HEARTBEAT_RUNNING_MAGIC;
		heartbeat_stopped_magic = (int) FOS_HEARTBEAT_STOPPED_MAGIC;
		/* incompatible heartbeats will ensure the user doesn't mix systems */
	}

	if (use_fos == 0)
	{
		sigprocmask (SIG_UNBLOCK, &sigs, NULL);
		executeCommand (config, flags, config->inactiveCommand);
		syncDaemon (config, flags, 0);
	}

	flags |= PULSE_FLAG_SUPPRESS_ERRORS;	/* Suppress useless start errs */

	if (flags & PULSE_FLAG_AMACTIVE)
	{
		if (use_fos)
		{
			sigprocmask (SIG_BLOCK, &sigs, NULL);
			fos_reserve_state = rs_active;
			SCSI_Process = reserveHold (config, flags);
			/* Timer is set, but we'll unset it and assume that it will
			   work.  If not, we'll reset ourselves anyway. */
			alarm (0);
			sigprocmask (SIG_UNBLOCK, &sigs, NULL);

			piranha_log (flags,
					(char *) "Forced active on startup: activating fos");
			HA_Process = activateFosServices (config, flags);
		}
		else
		{
			piranha_log (flags,
					(char *) "Forced active on startup: activating lvs");
			HA_Process = activateLvs (config, flags);
		}

	}
	else
	{
		if (use_fos)
		{
			piranha_log (flags, (char *) "Starting Failover Service Monitors");
			sigprocmask (SIG_BLOCK, &sigs, NULL);
			HA_Process = activateFosMonitors (config, flags);
			sigprocmask (SIG_UNBLOCK, &sigs, NULL);
		}
	}

	flags &= (~PULSE_FLAG_SUPPRESS_ERRORS);


	partner.sin_family = AF_INET;
	partner.sin_port = htons (config->heartbeatPort);

	if (flags & PULSE_FLAG_AMMASTER)
		memcpy (&partner.sin_addr, &config->backupServer,
				sizeof (struct in_addr));
	else
		memcpy (&partner.sin_addr, &config->primaryServer,
				sizeof (struct in_addr));

	if (sock_priv > -1)
	{
		partner_priv.sin_family = AF_INET;
		partner_priv.sin_port = htons (config->heartbeatPort);
		if (flags & PULSE_FLAG_AMMASTER)
			memcpy (&partner_priv.sin_addr, &config->backupPrivate,
					sizeof (struct in_addr));
		else
			memcpy (&partner_priv.sin_addr, &config->primaryPrivate,
					sizeof (struct in_addr));
	}

	gettimeofday (&now, NULL);
	needHeartBeat = sendHeartBeat = now;
	sendHeartBeat.tv_sec += config->keepAliveTime;
	needHeartBeat.tv_sec += config->deadTime;



	if (use_fos == 0)
	{
		sigemptyset (&sigs);
		sigaddset (&sigs, SIGCHLD);
	}
	else
		sigprocmask (SIG_UNBLOCK, &sigs, NULL);


	/*
	 **  The BIG loop!
	 */

	while (!done)
	{

		/*** ### Start of Block - disable incomplete processing of USR 1 & 2  ### */
		if (0)
		{
			if (User1Signal)
			{
				if (flags & PULSE_FLAG_AMACTIVE)
				{
					piranha_log (flags,
							(char *)
							"Terminating and restarting CLUSTER due to signal USR1");
					if (use_fos)
						deactivateFos (config, flags, -1);
					else
						deactivateLvs (config, flags);
					return 2;
				}
				else
				{
					piranha_log (flags,
							(char *)
							"Restarting JUST THIS NODE due to signal USR1 (and we are not the active node");
					if (use_fos)
						deactivateFos (config, flags, -1);
					else
						deactivateLvs (config, flags);
					return 2;
				}
			}
		}
		/* ### **** End of Block ********************************************* ### */


		if (needHeartBeat.tv_sec < sendHeartBeat.tv_sec)
			smaller = &needHeartBeat;
		else if (needHeartBeat.tv_sec > sendHeartBeat.tv_sec)
			smaller = &sendHeartBeat;
		else if (needHeartBeat.tv_usec < sendHeartBeat.tv_usec)
			smaller = &needHeartBeat;
		else
			smaller = &sendHeartBeat;

		if (pulse_debug)
		{
			if (smaller == &needHeartBeat)
				piranha_log (flags, "DEBUG -- setting NEED_heartbeat timer");
			else
				piranha_log (flags, "DEBUG -- setting SEND_heartbeat timer");
		}

		gettimeofday (&now, NULL);

		timeout.tv_sec = -1;

		if (smaller->tv_sec >= now.tv_sec)
			timeout.tv_sec = smaller->tv_sec - now.tv_sec;

		if (smaller->tv_usec >= now.tv_usec)
			timeout.tv_usec = smaller->tv_usec - now.tv_usec;
		else
		{
			timeout.tv_usec = (smaller->tv_usec + 1000000) - now.tv_usec;
			timeout.tv_sec--;
		}


		if (timeout.tv_sec >= 0)
		{
			int fds_to_process;
			int max_fd = sock;
			FD_ZERO (&fdset);
			FD_SET (sock, &fdset);

			if (sock_priv > -1)
			{
				FD_SET (sock_priv, &fdset);
				if (sock_priv > max_fd)
					max_fd = sock_priv;
			}

			if (check_links(config, &flags) == 0)
			{
				/* A link died.  Set shutdown flag. */
				if (!(flags & PULSE_FLAG_LINKFAIL))
				{
					piranha_log(flags,
							"Local NIC link failure: Pulse disabled");

					flags |= PULSE_FLAG_LINKFAIL;
					if (flags & PULSE_FLAG_AMACTIVE) 
						shutdown_services("local link failure");
					piranha_log(flags,"Stopping mysqld services");

					system("service mysqld stop");
					//Restarting to network to flush out virtual IPs created by onegate for virtual IP feature
					piranha_log(flags,"Stopping mysqld services");
					system("service network restart");
				}
			}
			else
			{
				if (flags & PULSE_FLAG_LINKFAIL)
				{
					/* Link up, time to be normal again */
					piranha_log(flags,
							"Local NIC link(s) restored: Pulse enabled");
					flags &= ~PULSE_FLAG_LINKFAIL;
					piranha_log(flags,"Restarting mysqld services");
					system("service mysqld restart");

				}
			}

			/*  There's a small race between this check and the select(), but
			    I don't know how to avoid it? */

			do
			{
				if (processSignals (config, &flags, use_fos, &sigs))
					return 0;

				fds_to_process = select (max_fd + 1, &fdset, 
						NULL, NULL, &timeout);
			}
			while (fds_to_process == -1 && errno == EINTR);

			if (fds_to_process < 0)
			{
				piranha_log (flags,
						(char *) "select() failed: %s", strerror (errno));
				return -1;

			}
			else if (fds_to_process == 0)
			{
				/* timed out, we must have something to do */
				timeout.tv_sec = -1;
			}
			else while (fds_to_process--)
			{
				int fd_to_process;

				if (sock_priv > -1 && FD_ISSET (sock_priv, &fdset))
					fd_to_process = sock_priv;
				else if (FD_ISSET (sock, &fdset))
					fd_to_process = sock;
				else break; /* shouldn't get here */

				FD_CLR (fd_to_process, &fdset);

				/* ADDED 16-Aug-2000 Lars Kellogg-Stedman <lars@larsshack.org>
				 * "size" was not being initialized properly.
				 */
				size = sizeof (struct sockaddr);
				rc = recvfrom (fd_to_process, &magic, sizeof (magic), 0,
						(struct sockaddr *) &sender, &size);

				if (rc < 0)
				{
					if (errno != ECONNREFUSED)
						piranha_log (flags, (char *) "recvfrom() failed: %s",
								strerror (errno));

				}
				else if (size != sizeof (sender))
				{
					piranha_log (flags,
							(char *)
							"Bad remote address size from recvfrom");

				}
				else if (memcmp (&partner.sin_addr, &sender.sin_addr,
							sizeof (partner.sin_addr)) &&
						(sock_priv == -1 ||
						 memcmp (&partner_priv.sin_addr, &sender.sin_addr,
							 sizeof (partner_priv.sin_addr))))
				{
					if (pulse_debug)
						piranha_log (flags,
								(char *) "Unexpected heartbeat from %s",
								inet_ntoa (sender.sin_addr));

				}
				else if (rc != sizeof (magic))
				{
					piranha_log (flags, (char *) "bad heartbeat packet");

				}
				else if ((magic != heartbeat_running_magic) &&
						(magic != heartbeat_stopped_magic) &&
						(magic != FOS_HEARTBEAT_STOPME_MAGIC) &&
						(magic != HEARTBEAT_HOLD_MAGIC) &&
						(magic != HEARTBEAT_HELD_MAGIC))
				{
					piranha_log (flags,
							(char *)
							"Incompatible heartbeat received -- other system not using identical services");


				}
				else
				{
					/*
					 **  At this point, we got our heartbeat and it will
					 **  tell us the state of the other machine.
					 **
					 **  If both are running, backup stops and master rearps. 
					 **  If neither is running, master starts. Otherwise,
					 **  keep the status quo.
					 */

					if (pulse_debug)
						piranha_log (flags, (char *)
								"DEBUG -- Received Heartbeat from partner");

					if (network_fault && fd_to_process == sock)
					{
						piranha_log (flags, "Network fault seems to be fixed");
						network_fault = 0;
						HA_Process = activateFosMonitors (config, flags);
					}

					/* Shared SCSI: if we had a reservation conflict and
					   went into 'do nothing' mode, this is where we
					   come out of it. */
					if (fos_reserve_state == rs_waiting)
					{
						fos_reserve_state = rs_backup;
						piranha_log (flags, "partner is alive");
						HA_Process = activateFosMonitors (config, flags);
					}

					if (magic == FOS_HEARTBEAT_STOPME_MAGIC)
					{

						/*
						 **  Special case: remote fos has told me that although
						 **  I am the master, it wants to win the argument
						 */

						if (((flags & PULSE_FLAG_AMACTIVE) != 0) &&
								((flags & PULSE_FLAG_AMMASTER) != 0))
						{
							/* Only applies if we are master and active */
							piranha_log (flags,
									(char *)
									"PARTNER HAS TOLD US TO GO INACTIVE!");
							reserveStop (config, flags);
							HA_Process = activateFosMonitors (config, flags);
							flags &= (~PULSE_FLAG_AMACTIVE);
							/* go inactive if not already */
						}
						/* else ignore -- we are already inactive */

						magic = heartbeat_running_magic;
						/* Other side was running */
					}


					/*** ### Start of Block - "hold" and "held" heartbeat logic incomplete ### */
					if (0)
					{

						if (magic == HEARTBEAT_HOLD_MAGIC)
						{

							/*
							 **  Special case: Remote active node has told us to
							 **  freeze until we see active heartbeats again.
							 */

							if (!(flags & PULSE_FLAG_AMACTIVE))
							{
								piranha_log (flags,
										(char *)
										"PARTNER HAS TOLD US TO GO INACTIVE AND WAIT!");
								deactivateFos (config, flags, -1);
								flags |= PULSE_FLAG_ONHOLD;
							}
							/* else ignore -- we are already inactive */

							magic = heartbeat_running_magic;
							/* Other side was running */
						}



						if (magic == HEARTBEAT_HELD_MAGIC)
						{

							/*
							 **  Special case: Remote node has told us they are
							 **  on hold until they see ACTIVE heartbeats from
							 **  us again.
							 */

							if (!(flags & PULSE_FLAG_AMACTIVE))
							{
								piranha_log (flags,
										(char *)
										"PARTNER HAS TOLD US TO GO INACTIVE AND WAIT!");
								deactivateFos (config, flags, -1);
								flags |= PULSE_FLAG_ONHOLD;
							}
							/* else ignore -- we are already inactive */

							magic = heartbeat_running_magic;
							/* Other side was running */
						}


					}
					/* ### **** End of Block ********************************************** ### */


					/*
					 **  We got a "running" (active) status from partner.
					 **  Compare with our states and process...
					 */

					if (magic == heartbeat_running_magic)
					{

						flags &= (~PULSE_FLAG_CHILDFAIL);

						if (((flags & PULSE_FLAG_AMACTIVE) != 0) &&
								((flags & PULSE_FLAG_AMMASTER) == 0))
						{
							/* Both are running and we are backup */

							/* lhh XXX Send one last heartbeat to make sure
							   the master knows we were operating.  This
							   will make it re-arp its VIPs */
							send_heartbeat(config, sock, sock_priv, &flags,
									&partner, &partner_priv);

							shutdown_services("partner active");
						}
						else if ((flags & PULSE_FLAG_AMACTIVE) &&
								(flags & PULSE_FLAG_AMMASTER))
						{

							/* Both are running & we are master */

							if (use_fos == 0)
							{
								piranha_log (flags,
										(char *)
										"partner active: resending arps");
								executeCommand (config, flags, config->activeCommand);
								sendLvsArps (config, flags);
							}
							else
								piranha_log (flags, (char *) "partner active");
						}

						/*
						 **  We got a "stopped" (inactive) status from partner.
						 **  Contrast & compare...
						 */

					}
					else if (magic == heartbeat_stopped_magic)
					{

						flags &= (~(PULSE_FLAG_FAILPARTNER |
									PULSE_FLAG_SUPPRESS_TC_MSG));

						/* Don't tell partner to go inactive if he has */
						/* Don't become active if we know our links are
						   dead or if we jsut caught SIGCHLD. */

						if (((flags & PULSE_FLAG_AMACTIVE) == 0) &&
								((flags & PULSE_FLAG_AMMASTER) != 0) &&
								((flags & PULSE_FLAG_LINKFAIL) == 0) &&
								((flags & PULSE_FLAG_CHILDFAIL) == 0))
						{
							startup_services("backup inactive");
						}

						if (((flags & PULSE_FLAG_AMACTIVE) == 0) &&
								((flags & PULSE_FLAG_AMMASTER) == 0) &&
								((flags & PULSE_FLAG_LINKFAIL) == 0) &&
								((flags & PULSE_FLAG_CHILDFAIL) == 0))
						{
							startup_services("primary inactive");
						}
					}

					gettimeofday (&needHeartBeat, NULL);
					needHeartBeat.tv_sec += config->deadTime;
				}

			}
		}



		/*
		 ** Send Heartbeat
		 */

		if (timeout.tv_sec < 0)
		{
			if (smaller == &sendHeartBeat)
			{
				send_heartbeat(config, sock, sock_priv, &flags,
						&partner, &partner_priv);

				gettimeofday (&sendHeartBeat, NULL);
				sendHeartBeat.tv_sec += config->keepAliveTime;

			}
			else /* smaller != &... */
			{
				if (!(flags & PULSE_FLAG_AMACTIVE) &&
						!(flags & PULSE_FLAG_LINKFAIL) &&
						!(flags & PULSE_FLAG_CHILDFAIL))
				{
					if (use_fos)
					{
						if (using_shared_SCSI (config, flags))
						{
							if (fos_reserve_state == rs_backup)
							{
								piranha_log (flags, "partner dead: reserving "
										"SCSI device");
								fos_reserve_state = rs_reserving_dev;
								SCSI_Process = reserveHold (config, flags);
								/* Timer is now set.  See the top of the loop,
								   where SIGALRM gets dealt with, to see what
								   happens next. */
							}
						}
						else
						{
							piranha_log (flags, "partner dead: activating "
									"failover services");
							flags |= PULSE_FLAG_AMACTIVE;
							HA_Process = activateFosServices (config, flags);
							flags &= (~(PULSE_FLAG_SUPPRESS_TC_MSG |
										PULSE_FLAG_FAILPARTNER));
						}
					}
					else
					{
						piranha_log (flags,
								(char *) "partner dead: activating lvs");
						HA_Process = activateLvs (config, flags);
						flags |= PULSE_FLAG_AMACTIVE;
					}
				}
				else
				{
					/* Partner is dead, and we are already active */
					flags &= (~(PULSE_FLAG_SUPPRESS_TC_MSG |
								PULSE_FLAG_FAILPARTNER));
				}

				gettimeofday (&needHeartBeat, NULL);
				needHeartBeat.tv_sec += config->deadTime;
			} /* else (smaller != ... ) */
		}
	}

	return 0;
}


/*
 **  amMaster() --   Determine if we are primary or backup node.
 */

	static int
amMaster (struct lvsConfig *config, int flags)
{

	FILE *fp;
	int res = 0;
	char buf[1024];
	char *start, *end;
	int s = socket (AF_INET, SOCK_DGRAM, 0);
	struct in_addr addr;

	if ((fp = fopen ("/proc/net/dev", "r")) == NULL)
	{
		piranha_log (flags, (char *) "failed to open /proc/net/dev!\n");
		close (s);
		return 1;
	}

	while (fgets(buf, sizeof(buf), fp))
	{
		for (start = buf; isspace(*start); start++);
		end = strchr (start, ':');
		if (!end)
			continue;
		*end = '\0';

		if (!getInterfaceAddress (s, start, &addr, flags) &&
				!memcmp (&addr, &config->primaryServer, sizeof (addr)))
		{
			res = 1;
			break;
		}
	}

	close(s);
	fclose(fp);

	return res;
}



	int
main (int argc, const char *argv[], char *env[])
{
	int noDaemon = 0;
	int flags = 0;
	int rc = 0;
	int fd, fd_priv;
	int line;
	struct lvsConfig config;
	int master = 0;
	int noRun = 0;
	int teststart = 0;
	int forceActive = 0;
	poptContext optCon;
	char *fargv[40];
	char **fargp = fargv;
	pid_t child;
	int i;
	int service_count = 0;
	int display_ver = 0;

	struct poptOption options[] = {

		{"configfile", 'c', POPT_ARG_STRING, &configFile, 0,
			N_("Configuration file"), N_("configfile")},

		{"nodaemon", 'n', 0, &noDaemon, 0,
			N_("Don't run as a daemon")},

		{"test-start", 't', 0, &teststart, 0,
			N_("Don't actually run lvs or fos")},

		{"verbose", 'v', 0, &pulse_debug, 0,
			N_("Display debugging information")},

		{"forceactive", '\0', 0, &forceActive, 0,
			N_("Force into the active state")},

		{"lvsd", '\0', POPT_ARG_STRING, &lvsBinary, 0,
			N_("Location of lvs binary (defaults /usr/sbin/lvsd)")},

		{"fos", '\0', POPT_ARG_STRING, &fosBinary, 0,
			N_("Location of fos binary (defaults /usr/sbin/fos)")},

		{"norun", '\0', 0, &noRun, 0,
			N_("Same as 'test-start'")},

		{"version", '\0', 0, &display_ver, 0,
			N_("Display program version")},

		POPT_AUTOHELP

			POPT_TABLEEND

	};

	optCon = poptGetContext("pulse", argc, argv, options, 0);
	poptReadDefaultConfig(optCon, 1);

	if((rc = poptGetNextOpt(optCon)) < -1)
	{
		fprintf(stderr, _("pulse: bad argument %s: %s\n"),
				poptBadOption (optCon, POPT_BADOPTION_NOALIAS),
				poptStrerror (rc));
		return 1;
	}

	if (poptGetArg (optCon))
	{
		fprintf (stderr, _("pulse: unexpected arguments\n"));
		return 1;
	}

	poptFreeContext (optCon);
	logName ((char *) "pulse");

	if (display_ver)
	{
		printf ("Program Version:\t%s\n", _PROGRAM_VERSION);
		printf ("Built:\t\t\t%s\n", DATE);	/* DATE epulled from Makefile */
		printf ("A component of:\t\tpiranha-%s-%s\n", VERSION, RELEASE);
		return 0;
	}


	if (noDaemon)
		flags |= LVS_FLAG_PRINTF;
	else
		flags |= PULSE_FLAG_ASDAEMON | LVS_FLAG_SYSLOG;

	if (forceActive)
		flags |= PULSE_FLAG_AMACTIVE;

	if (noRun | teststart)
		flags |= PULSE_FLAG_NORUN;

	if ((fd = open (configFile, O_RDONLY)) < 0)
	{
		fprintf (stderr, "pulse: failed to open %s\n", configFile);
		return 1;
	}

	if ((rc = lvsParseConfig (fd, &config, &line)))
	{
		fprintf (stderr, "pulse: error parsing %s at line %d: %s\n",
				configFile,line,lvsStrerror(rc));
		return 1;
	}


	lvsRelocateFS (&config);
	/* Move failover services and Clean up virtual server list */

	close (fd);

	master = amMaster (&config, flags);
	flags |= master ? PULSE_FLAG_AMMASTER : 0;

	if (config.backupActive)
	{
		if (config.backupServer.s_addr == 0)
		{
			config.backupActive = 0;
			piranha_log (flags,
					(char *)
					"Undefined backup node marked as active? -- clearing that!");
		}
	}


	if (!rc)
	{
		if (config.lvsServiceType == LVS_SERVICE_TYPE_FOS)
		{
			for (i = 0; i < config.numFailoverServices; ++i)
			{
				if (config.failoverServices[i].isActive)
					++service_count;
			}

			if (!service_count)
			{
				fprintf (stderr,
						"pulse: no active fos services defined in %s\n",
						configFile);
				return 1;
			}
		}
		else
		{
			for (i = 0; i < config.numVirtServers; ++i)
			{
				if (config.virtServers[i].isActive)
					++service_count;
			}
			if (!service_count)
			{
				fprintf (stderr,
						"pulse: no active lvs services defined in %s\n",
						configFile);
				return 1;
			}
		}
	}


	if ((fd = createSocket (master ? &config.primaryServer :
					&config.backupServer, config.heartbeatPort,
					flags)) < 0)
	{
		fprintf (stderr,
				"pulse: cannot create heartbeat socket. running as root?\n");
		return 1;
	}

	if (memcmp (&config.primaryServer, &config.primaryPrivate,
				sizeof (config.primaryServer))) 
	{
		if ((fd_priv = createSocket (master ? &config.primaryPrivate :
						&config.backupPrivate, config.heartbeatPort,
						flags)) < 0)
		{
			fprintf (stderr, "pulse: cannot create heartbeat socket. "
					"running as root?\n");
			return 1;
		}
	}
	else
		fd_priv = -1;

	if (flags & PULSE_FLAG_ASDAEMON)
	{
		if (daemonize (flags))
			exit (0);
	}

	if (flags & LVS_FLAG_SYSLOG)
	{
		openlog ("pulse", LOG_PID, LOG_DAEMON);
	}

	if (master)
	{
		piranha_log (flags, (char *) "STARTING PULSE AS MASTER");

		if (!config.backupActive)
		{
			if ((config.lvsServiceType == LVS_SERVICE_TYPE_FOS) &&
					(forceActive == 0))
			{
				piranha_log (flags,
						(char *)
						"Warning: FOS backup node not defined -- skipping checks");
				flags |= PULSE_FLAG_AMACTIVE;
			}
		}
	}
	else if (!config.backupActive)
	{
		piranha_log (flags,
				(char *)
				"We are backup node and backup is marked inactive -- exiting pulse");
		rc = -1;
	}
	else
		piranha_log (flags, (char *) "STARTING PULSE AS BACKUP");


	if (!rc)
		rc = run (&config, fd, fd_priv, flags);

	if (flags & PULSE_FLAG_ASDAEMON)
		unlink ("/var/run/pulse.pid");


	if (rc == 2)
		rc = 0;
	/* ### Turn off incomplete USR1 processing (Disables next block) ### */

	if (rc == 2)
	{
		piranha_log (flags, (char *) "Starting a new copy of pulse...");
		/* a 2 means we are to restart ourselves */

		close (fd);
		rc = 0;

		*fargp++ = (char *) "/usr/sbin/pulse";
		*fargp++ = (char *) "-c";
		*fargp++ = configFile;

		if (config.lvsServiceType == LVS_SERVICE_TYPE_FOS)
		{
			*fargp++ = (char *) "--fos";
			*fargp++ = fosBinary;
		}
		else
		{
			*fargp++ = (char *) "--lvs";
			*fargp++ = lvsBinary;
		}

		if (pulse_debug)
			*fargp++ = (char *) "-v";

		if (flags & PULSE_FLAG_NORUN)
			*fargp++ = (char *) "--norun";

		if (flags & PULSE_FLAG_ASDAEMON)
			*fargp++ = (char *) "--nodaemon";

		*fargp = NULL;

		logArgv (flags, fargv);

		if (!(child = fork ()))
		{
			execv (fargv[0], fargv);
			exit (-1);
		}
	}

	return rc;
}
