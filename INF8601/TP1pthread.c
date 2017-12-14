/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#define stacksize 2097152 // on donne une grosseur de 2 Mb pour notre stack, sa devrait etre suffisant
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>

#include "dragon.h"
#include "color.h"
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "dragon_pthread.h"
int gettid(void);
pthread_mutex_t mutex_stdout;

void printf_threadsafe(char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

void *dragon_draw_worker(void *data){
  /* on initialise nos variable, code_erreur est evident, tdat sont nos donnes pour chaque 
	 threads, *canvas sont now points de depart pour l'initialisation de la surface. On doit 
	 utiliser des differents points de depart vu que le range n'est pas le meme que notre start et
	 end habituel  */
  int code_erreur;
  struct draw_data* tdat = (struct draw_data *) data;
  int startcanvas = (tdat->id)*((tdat->dragon_width * tdat->dragon_height)/tdat->nb_thread);
  int endcanvas = (tdat->id + 1) * ((tdat->dragon_width*tdat->dragon_height)/tdat->nb_thread);
  int start = (tdat->id)*(tdat->size/tdat->nb_thread);
  int end = (tdat->id + 1) * (tdat->size / tdat->nb_thread);


  //cette ligne sert a debugger les points de depart dans les differentes threads.
  //elle indique les points de depart de chaque threads dans le terminal
  
  //printf("start is %d, end is %d, i is %d\n",start, end, tdat->id);
  
	/* 1. Initialiser la surface */
  //on initialise avec nos variables dependamment du thread ou l'on se situe
  
  init_canvas(startcanvas, endcanvas, tdat->dragon, -1);
	/* 2. Dessiner le dragon */
  //on dessine avec nos variables

  //printf("nous sommes rendu au wait de la thread %d\n", tdat->id);
  code_erreur = pthread_barrier_wait(tdat->barrier);
  //printf("nous avons passer la barriere %d\n", tdat->id);

  if(code_erreur != -1 && 0){
    //error handling de la barriere, cependant selon les man pages de barrier_wait, ne devrait jamais se produire
    printf("erreur dans la barriere %d, code %d\n",tdat->id, code_erreur);
  }

  
  dragon_draw_raw(start, end, tdat->dragon, tdat->dragon_width, tdat->dragon_height, tdat->limits, tdat->id);
  
	/* 3. Effectuer le rendu final */

  /**/
  
  
  
  return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
  //TODO("dragon_draw_pthread");

	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	pthread_attr_t Attr;
	limits_t lim;
	struct draw_data info;
	char *dragon = NULL;
	int i;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;
	void * status;
	int code_erreur;

	pthread_attr_init(&Attr);

	threads =  malloc(sizeof(pthread_t)* nb_thread);
	pthread_attr_setstacksize(&Attr, stacksize);
	pthread_attr_setdetachstate(&Attr, PTHREAD_CREATE_JOINABLE);

	data = malloc((sizeof(struct draw_data)) * (nb_thread+1));

	
	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	/* 1. Initialiser barrier. */
	pthread_barrier_init(&barrier, NULL, nb_thread +1);
	

	if (dragon_limits_pthread(&lim, size, nb_thread) < 0)
		goto err;

	info.dragon_width = lim.maximums.x - lim.minimums.x;
	info.dragon_height = lim.maximums.y - lim.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height)) == NULL) {
		printf("malloc error dragon\n");
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = lim;
	info.barrier = &barrier;
	info.palette = palette;
	info.dragon = dragon;
	info.image = image;

	for(i = 0; i < nb_thread; i++){
	  data[i] = info;
	  data[i].id = i;
	}

	/* 2. Lancement du calcul parallèle principal avec draw_dragon_worker */

	for(i = 0; i< nb_thread; i++){
	  code_erreur = pthread_create(&threads[i], &Attr, dragon_draw_worker, (void *) &data[i]);
	  if(code_erreur){
		printf("erreur dans la creation de la thread %d dans draw_dragon_pthread, code %d \n",i, code_erreur);
		goto err;
	  }
	}

	/* 3. Attendre la fin du traitement */
	//printf("nous sommes rendu au wait de la main\n");
	pthread_barrier_wait(&barrier);
	//printf("nous avons passer la barrier de la main \n");
	for(i = 0; i< nb_thread; i++){
	  code_erreur = pthread_join(threads[i], &status);
	  if(code_erreur){
		printf("erreur dans le join de la thread %d dans draw_dragon_pthread, erreur code %d \n", i, code_erreur);
		goto err;
	  }
	}
	scale_dragon(0, height, image, width, height, dragon, info.dragon_width, info.dragon_height, palette);
	/* 4. Destruction des variables (à compléter). */

	/*la plupart des variable a detruire sont tous dans "done", la seule exception etant status et info. info genere une erreur de segmentation si on la detruit alors 
	  j'assume qu'elle est utiliser ailleur. */
	free(status);
	goto done;
done:
	FREE(data);
	FREE(threads);
	free_palette(palette);
	*canvas = dragon;
	return ret;

err:
	FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data)
{
  struct limit_data *lim = (struct limit_data *) data;
  
  //cette ligne imprime le TID de la thread
  printf("tid of thread is %d\n",(int) gettid());
  
  piece_limit(lim->start, lim->end, &lim->piece);
  return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread)
{
	void * status;
	int ret = 0;
	int i;
	pthread_t *threads = NULL;
	pthread_attr_t ptAttr;
	pthread_attr_init(&ptAttr);	
	struct limit_data *thread_data;  
	piece_t master;
	int code_erreur;
	pthread_attr_setdetachstate(&ptAttr, PTHREAD_CREATE_JOINABLE);

	piece_init(&master);

	/* 1. ALlouer de l'espace pour threads et threads_data. */
	
	//on associe la valeur defini de stack a nos pthreads et on fait un malloc
	// avec le nombre de limit_data par nombre de threads
	
	pthread_attr_setstacksize(&ptAttr, stacksize);
	threads = malloc(sizeof(pthread_t)*(nb_thread));
	thread_data = malloc((sizeof(struct limit_data)) * (nb_thread));
	
	/* 2. Lancement du calcul en parallèle avec dragon_limit_worker. */
	
	/*initialisation des donnees pour les differentes thread, on divise les valeurs entre 
	 *start* et *end* par le nombre de threads que nous avons. 
	 start --------------------------------------------------------------------> end
	 Thread1 ----> Thread 2 ----> Thread 3 ----> ..... ------------------------> end*/
	
	for(i = 0; i < nb_thread ; i++){
	  thread_data[i].id = i;
	  thread_data[i].start = (int)(i)*(size/nb_thread);
	  thread_data[i].end = (int)(i+1)*(size/nb_thread);
	  thread_data[i].piece = master;


	  //cette ligne sert a debugger les differentes threads, veuillez enlever le comment pour voir les points de
	  //depart de chaque thread dans le terminal
	  
	  
	  printf("start is %d, end is %d, id of thread is %d\n",(int)thread_data[i].start, (int)thread_data[i].end, (int)thread_data[i].id);
	}
	// on creer nos threads avec dragon_limit_worker et nos differents points de depart
	for(i = 0; i < nb_thread ; i++){
	  code_erreur = pthread_create(&threads[i],&ptAttr, dragon_limit_worker, (void *) &thread_data[i]);
	  if (code_erreur){
		printf("nous avons une erreur dans la thread %d",i);
		goto err;
	  }
	}
	//on join les threads lorsqu'ils ont finient le travaille et on merge les differents morceau.
	for(i = 0; i < nb_thread ; i++){
	  code_erreur = pthread_join(threads[i], &status);
	  if(code_erreur){
		printf("nous avons une erreur dans le join de la thread %d", i);
		goto err;
	  }
	  piece_merge(&master, thread_data[i].piece);
	}
	
	/* 3. Attendre la fin du traitement. */
	goto done;
done:
	FREE(threads);
	FREE(thread_data);
	*limits = master.limits;
	return ret;
err:
	ret = -1;
	goto done;
}
