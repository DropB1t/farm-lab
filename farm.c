/*
 * Progetto del corso di Laboratorio II 2021/22
 *
 * Dipartimento di Informatica Università di Pisa
 *
 * Studente: Yuriy Rymarchuk
 * Matricola: 614484
 * E-mail: yura.ita.com@gmail.com
 */

/**
 * @file farm.c
 * @author Yuriy Rymarchuk
 * @brief File main del progetto
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

/*----- Includes Personali -----*/
#include "util.h"
#include "boundedqueue.h"

/*----- DEFINES -----*/
#define EOS (void *)0x1
#define NAME_MAX 256
#define N_THREADS 4L
#define Q_LEN 8L
#define DELAY 0L
#define RECONNECT 50000

#define UNIX_PATH_MAX 108
#define SOCKNAME "./sck_y"

typedef struct f_struct
{
	char *filename;
	size_t filesize;
} f_struct_t;

typedef struct th_struct
{
	int fd_skt;
	sem_t sem;
	BQueue_t *q;
} th_struct_t;

volatile sig_atomic_t sig_term = 0;

/*----- Funzioni -----*/

/**
 * @brief    In caso di chiamata del main con parametri sbagliati, suggerisce come va fatta la chiamata
 *
 * @param    progname nome del programma
 */
static void
format_cmd(const char *progname)
{
	fprintf(stderr, "Il programma va lanciato con il seguente comando:\n");
	fprintf(stderr, "\n\t./%s [OPTION]... [FILES LIST]...\n\n", progname);
	fprintf(stderr, "-n\n    numero di thread (default 4)\n");
	fprintf(stderr, "-q\n    lunghezza delal coda concorrente (default 8)\n");
	fprintf(stderr, "-t\n    tempo in ms tra l'invio delle richieste ai thread Worker (default 0)\n");
	fflush(stdout);
}

/**
 * @brief	Controlla se i valori dei parametri opzionali sono del tipo giusto e nel caso positivo li assegna alla variabile passata
 *
 * @param	val valore da controllare
 * @param	var variabile a cui viene assegnato il valore se passa il controllo
 */
static void
check_param(char *val, long *var)
{
	int r = isNumber(val, var);
	if (r == 1)
	{
		fprintf(stderr, "%s è NaN\n", val);
		fflush(stdout);
		exit(1);
	}
	else if (r == 2)
	{
		fprintf(stderr, "%s ha provocato un overflow/underflow\n", val);
		fflush(stdout);
		exit(2);
	}
}

/**
 * @brief	Fa il test sull'espressione 'test' passata come parametro per determinare se è verificato un errore
 *
 * @param	test condizione che verifica l'errore
 * @param	message messaggio sa stampare nel caso dell'errore
 * @param	__VA_ARGS__
 */
static void
check(int test, const char *message, ...)
{
	if (test)
	{
		va_list args;
		va_start(args, message);
		vfprintf(stderr, message, args);
		va_end(args);
		fprintf(stderr, "\n");
		exit(EXIT_FAILURE);
	}
}

/**
 * @brief	Il corpo del processo Collector
 *
 * @param	sa indirizzo della connessione socket AF_UNIX
 */
static void Collector(struct sockaddr_un sa);

/**
 * @brief	Aspetta la terminazione del processo collector e stampa lo status con cui termina il processo
 *
 * @param	pid ID del processo collector
 */
static void
collector_exit_status(pid_t pid)
{
	int status;
	if (waitpid(pid, &status, 0) == -1)
	{
		perror("Waitpid");
		exit(EXIT_FAILURE);
	}

	if (WIFEXITED(status))
	{
		DBG("Processo collector [pid=%d] è terminato con exit(%d)\n", pid, WEXITSTATUS(status));
	}
	else
	{
		DBG("Processo collector [pid=%d] è terminato con un exit irregolare\n", pid);
	}
	fflush(stdout);
}

/**
 * @brief	Start routine dei thread Worker
 */
static void *Worker(void *arg);

/**
 * @brief	Routine per liberare la memoria di elementi f_struct
 */
void F(void *el)
{
	if (el != EOS)
	{
		f_struct_t *f = el;
		free(f->filename);
		free(f);
	}
}

/**
 * @brief	Mappa un file di interi long
 *
 * @param	file_name nome del file
 * @param	contents_ptr puntatore che punta alla locazione di memoria mappata
 * @param	size dimensione della memoria in byte da mappare
 */
void mmap_file(const char *file_name, long **contents_ptr, size_t size);

/*----- GESTORE DEI SEGNALI -----*/
/**
 * @function signal_handler
 * @brief    gestisce i segnali ricevuti dal server, usata dal thread
 *           gestore dei segnali
 *
 * Si mette in attesa finchè non arrivano alcuni segnali e all' arrivo
 * esegue l' azione prevista
 */
void *Signal_Handler(void *arg)
{

	sigset_t *sigptr = (sigset_t *)arg;
	sigset_t sigset = *sigptr;

	int n;
	int signal;
	while (sig_term == 0)
	{
		n = sigwait(&sigset, &signal);

		if (n != 0)
		{
			errno = n;
			perror("nella sigwait");
			exit(EXIT_FAILURE);
		}

		if ((signal == SIGINT) || (signal == SIGQUIT) || (signal == SIGTERM) || (signal == SIGHUP))
		{
			sig_term = 1;
		}

		if (signal == SIGUSR1)
		{
			break;
		}
	}

	pthread_exit(NULL);
}

/*----- MAIN -----*/
int main(int argc, char *argv[])
{
	if (argc == 1)
	{
		format_cmd("farm");
		return 0;
	}

	long n = N_THREADS;
	long q_len = Q_LEN;
	long delay = DELAY;

	int opt;
	while ((opt = getopt(argc, argv, ":n:q:t:")) != -1)
	{
		switch (opt)
		{
		case 'n':
			DBG("Numero di thread: %s\n", optarg);
			check_param(optarg, &n);
			break;
		case 'q':
			DBG("Lunghezza coda: %s\n", optarg);
			check_param(optarg, &q_len);
			break;
		case 't':
			DBG("Delay: %s\n", optarg);
			check_param(optarg, &delay);
			break;
		case ':':
			fprintf(stderr, "opzione %c è stata passata senza un valore\n", opt);
			return 1;
		case '?':
			fprintf(stderr, "opzione sconosciuta: -%c\n", optopt);
			return 1;
			break;
		}
	}

	int err;
	/*----- SIGNALS SETUP -----*/
	struct sigaction s;
	memset(&s, 0, sizeof(s));
	s.sa_handler = SIG_IGN;

	errno = 0;
	err = sigaction(SIGPIPE, &s, NULL);
	check(err == -1, "Errore nell'ignorare sengale SIGPIPE: %s", strerror(errno));

	sigset_t set;
	pthread_t sig_handler;

	errno = 0;
	err = sigemptyset(&set);
	check(err == -1, "Funzione sigemptyset ha fallito: %s", strerror(errno));
	errno = 0;
	err = sigaddset(&set, SIGINT);
	check(err == -1, "Funzione sigaddset ha fallito: %s", strerror(errno));
	errno = 0;
	err = sigaddset(&set, SIGQUIT);
	check(err == -1, "Funzione sigaddset ha fallito: %s", strerror(errno));
	errno = 0;
	err = sigaddset(&set, SIGTERM);
	check(err == -1, "Funzione sigaddset ha fallito: %s", strerror(errno));
	errno = 0;
	err = sigaddset(&set, SIGHUP);
	check(err == -1, "Funzione sigaddset ha fallito: %s", strerror(errno));
	errno = 0;
	err = sigaddset(&set, SIGUSR1);
	check(err == -1, "Funzione sigaddset ha fallito: %s", strerror(errno));

	err = pthread_sigmask(SIG_SETMASK, &set, NULL);
	check(err != 0, "Funzione pthread_sigmask ha fallito: %s", strerror(err));

	err = pthread_create(&sig_handler, NULL, Signal_Handler, &set);
	check(err != 0, "pthread_create sig_handler ha fallito: %s", strerror(err));

	/*----- SOCKET SETUP -----*/

	int fd_skt;
	struct sockaddr_un sa;
	strncpy(sa.sun_path, SOCKNAME, UNIX_PATH_MAX);
	sa.sun_family = AF_UNIX;

	pid_t collector_pid = fork();
	if (collector_pid == 0)
	{
		Collector(sa);
		exit(EXIT_SUCCESS);
	}

	/*----- CLIENT SETUP -----*/

	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	while (connect(fd_skt, (struct sockaddr *)&sa, sizeof(sa)) == -1)
	{
		if (errno == ENOENT)
			usleep(RECONNECT); // Aspetta 50ms per riprovare la connessione
		else
			exit(EXIT_FAILURE);
	}

	/*----- MASTER WORKER -----*/

	th_struct_t *th_struct = malloc(sizeof(th_struct_t));
	assert(th_struct);

	th_struct->fd_skt = fd_skt;

	errno = 0;
	err = sem_init(&th_struct->sem, 1, 1);
	check(err == -1, "sem_init ha fallito: %s", strerror(errno));

	errno = 0;
	BQueue_t *q = initBQueue(q_len);
	check(q == NULL, "initBQueue ha fallito: %s", strerror(errno));
	th_struct->q = q;

	pthread_t th[n];
	for (size_t i = 0; i < n; i++)
	{
		err = pthread_create(&th[i], NULL, Worker, th_struct);
		check(err != 0, "pthread_create ha fallito (Worker n.%ld): %s", i, strerror(err));
	}

	/*----- TEST DEI FILE -----*/

	for (size_t i = optind; i < argc && sig_term != 1; i++)
	{
		size_t filesize;
		errno = 0;
		if (isRegular(argv[i], &filesize) != 1)
		{
			if (errno == 0)
			{
				fprintf(stderr, "%s non e' un file regolare\n", argv[i]);
				continue;
			}
			perror("isRegular");
			continue;
		}
		f_struct_t *file = malloc(sizeof(f_struct_t));
		file->filename = strdup(argv[i]);
		file->filesize = filesize;

		usleep(delay * 1000);
		push(q, file);
	}
	push(q, EOS);

	if (sig_term == 1)
	{
		err = pthread_join(sig_handler, NULL);
		check(err != 0, "pthread_join di sig_handler ha fallito: %s\n", strerror(err));
	}
	else
	{
		pthread_kill(sig_handler, SIGUSR1);
	}

	for (size_t i = 0; i < n; i++)
	{
		err = pthread_join(th[i], NULL);
		check(err != 0, "pthread_join ha fallito (Worker n.%ld): %s\n", i, strerror(err));
	}

	// Gestire error check delle chiamate sottostanti
	deleteBQueue(th_struct->q, NULL);

	errno = 0;
	err = sem_destroy(&th_struct->sem);
	check(err == -1, "sem_destroy ha fallito: %s\n", strerror(errno));

	close(th_struct->fd_skt);
	free(th_struct);
	collector_exit_status(collector_pid);
	
	errno = 0;
	err = unlink(SOCKNAME);
	check(err == -1, "unlink del socket %s ha fallito: %s\n",SOCKNAME, strerror(errno));

	return 0;
}

/*----- COLLECTOR -----*/
static void
Collector(struct sockaddr_un sa)
{
	int fd_skt, fd_c;
	DBG("Collector is up\n", NULL);

	/*----- SERVER SETUP -----*/
	int r;

	errno = 0;
	fd_skt = socket(AF_UNIX, SOCK_STREAM, 0);
	check(fd_skt == -1, "Creazione socket nel Collector ha fallito: %s", strerror(errno));

	errno = 0;
	r = bind(fd_skt, (struct sockaddr *)&sa, sizeof(sa));
	check(r == -1, "Bind del socket nel Collector ha fallito: %s", strerror(errno));

	errno = 0;
	r = listen(fd_skt, SOMAXCONN);
	check(r == -1, "Listen nel socket del Collector ha fallito: %s", strerror(errno));

	errno = 0;
	fd_c = accept(fd_skt, NULL, 0);
	check(fd_c == -1, "Accetazione del client nel Collector ha fallito: %s", strerror(errno));

	int N = 1024;
	char buf[N];
	while (1)
	{
		errno = 0;
		int r = read(fd_c, buf, N);
		check(r == -1, "Funzione read dal socket nel Collector ha fallito: %s", strerror(errno));
		if (r == 0)
		{
			break;
		}
		printf("%s\n", buf);
	}

	close(fd_skt);
	close(fd_c);
}

static void *Worker(void *arg)
{
	th_struct_t *th_struct = arg;
	DBG("Start della routine del Worker\n", NULL);
	while (1)
	{
		f_struct_t *f = NULL;
		f = pop(th_struct->q);
		if (f == EOS)
		{
			break;
		}
		DBG("File ricevuto: %s con dimensione di %ld bytes\n", f->filename, f->filesize);

		/*----- Calcolo di result -----*/

		long *content = NULL;
		mmap_file(f->filename, &content, f->filesize);

		long result = 0;
		for (size_t i = 0; i < f->filesize / 8; i++)
		{
			result += (i * content[i]);
		}

		int len = (snprintf(NULL, 0, "%ld %s", result, f->filename)) + 1;
		char res[len];
		snprintf(res, len, "%ld %s", result, f->filename);

		P(&th_struct->sem);
		errno = 0;
		int r = write(th_struct->fd_skt, res, len);
		check(r == -1, "Funzione write nel Worker ha fallito: %s", strerror(errno));
		V(&th_struct->sem);

		munmap(content, f->filesize);
		free(f->filename);
		free(f);
	}
	push(th_struct->q, EOS);
	DBG("Chiusura del Worker\n", NULL);
	pthread_exit(NULL);
}

void mmap_file(const char *file_name, long **content_ptr, size_t size)
{
	int fd;
	fd = open(file_name, O_RDONLY);
	check(fd < 0, "Funzione open %s ha fallito: %s", file_name, strerror(errno));
	*content_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
	check(*content_ptr == MAP_FAILED, "Funzione mmap %s ha fallito: %s", file_name, strerror(errno));
	close(fd);
}