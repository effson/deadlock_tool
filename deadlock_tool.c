#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

#if 1

typedef unsigned long int uint64;

#define MAX 100

enum Type {PROCESS, RESOURCE};

struct source_type {
    uint64 id;
    enum Type type;

    uint64 lock_id;
    int degree;
};

struct vertex {
    struct source_type s_type;
    struct vertex *next;
};

struct task_graph {
    struct vertex list[MAX];
    int num;

    struct source_type locklist[MAX];
    int lockidx;

    pthread_mutex_t mutex;
};

struct task_graph *tg = NULL;
int path[MAX + 1];
int visited[MAX];
int k = 0;
int deadlock = 0;

struct vertex *create_vertex(struct source_type s_type) {
    struct vertex *vtx = (struct vertex *)malloc(sizeof(struct vertex));
    vtx->s_type = s_type;
    vtx->next = NULL;

    return vtx;
}

int search_vertex(struct source_type s_type) {
    int i = 0;
    for (i = 0; i < tg->num; i++) {
        if (tg->list[i].s_type.id == s_type.id && tg->list[i].s_type.type == s_type.type) {
            return i;
        }
    }
    return -1;
}

void add_vertex(struct source_type s_type) {
    if (search_vertex(s_type) == -1) {
        tg->list[tg->num].s_type = s_type;
        tg->list[tg->num].next = NULL;
        tg->num++;
    }
}

void add_edge(struct source_type from, struct source_type to) {
    add_vertex(from);
    add_vertex(to);

    struct vertex *vtx = &tg->list[search_vertex(from)];
    while (vtx->next != NULL) {
        vtx = vtx->next;
    }
    vtx->next = create_vertex(to);
}

int verify_edge(struct source_type i, struct source_type j) {
    if (tg->num == 0) {
        return 0;
    }

    int idx = search_vertex(i);
    if (idx == -1) {
        return 0;
    }

    struct vertex *vtx = &tg->list[idx];
    while (vtx != NULL) {
        if (vtx->s_type.id == j.id) {
            return 1;
        }
        vtx = vtx->next;
    }
    return 0;
}

void remove_edge(struct source_type i, struct source_type j) {

    int i_idx = search_vertex(i);
    int j_idx = search_vertex(j);
    if (i_idx != -1 && j_idx != -1) {
        struct vertex *vtx = &tg->list[i_idx];
        struct vertex *remove;

        while (vtx->next != NULL) {
            if (vtx->next->s_type.id == j.id) {
                remove = vtx->next;
                vtx->next = vtx->next->next;
                free(remove);
                break;
            }
            vtx = vtx->next;
        }
    }
    
}

int search_empty_lock(uint64_t lockaddr) {
    int i = 0;
    for (i = 0; i < MAX; i++) {
        if (tg->locklist[i].id == 0 && tg->locklist[i].lock_id == 0) {
            tg->locklist[i].type = RESOURCE; 
            tg->locklist[i].degree = 0;
            return i;
        }
    }
    return -1; 
}

int search_lock(uint64_t lockaddr) {
    int i = 0;
    for (i = 0; i < tg->lockidx; i++) {
        if (tg->locklist[i].lock_id == lockaddr) {
            return i;
        }
    }
    return -1;
}

void print_deadlock() {

    int i = 0;
    printf("deadlock cycle detected: ");
	int first = -1;
    for (i = 0; i < k - 1; i++) {
		if(tg->list[path[i]].s_type.id == 0) continue;
		if(first == -1) first = i;
        printf("%ld--> ", tg->list[path[i]].s_type.id);
    }
    printf("%ld-->", tg->list[path[i]].s_type.id);
	  printf("%ld\n", tg->list[path[first]].s_type.id);
}

int DFS(int idx) {
    struct vertex *vtx = &tg->list[idx];
    if (visited[idx] == 1) {
        path[k++] = idx;
        print_deadlock();
        deadlock = 1;

        return 0;
    }
    visited[idx] = 1;
    path[k++] = idx;

    while (vtx->next != NULL) {
        DFS(search_vertex(vtx->next->s_type));
        k--;
        vtx = vtx->next;
    }

    return 1;
}

int search_for_cycle(int idx) {
    struct vertex *vtx = &tg->list[idx];
    visited[idx] = 1;
    k = 0;
    path[k++] = idx;

    while (vtx->next != NULL) {
        int i = 0;
        for (i = 0; i < tg->num; i++) {
           if (i == idx) {
                continue;
            }
           visited[i] = 0;
        }

        for (i = 0; i <= MAX;i ++) {
            path[i] = -1;
        }
        k = 1;
        DFS(search_vertex(vtx->next->s_type));
        vtx = vtx->next;
    }
}

#endif

void lock_before(uint64_t tid, uint64_t lockaddr) {
    int idx = 0;
    for (idx = 0; idx < tg->lockidx; idx++) {
        if (tg->locklist[idx].lock_id == lockaddr) { 
            struct source_type from;
            struct source_type to;

            from.id = tid;
            to.id = tg->locklist[idx].id;

            from.type = PROCESS;
            to.type = PROCESS;

            add_vertex(from);
            add_vertex(to);

            tg->locklist[idx].degree ++;

            if (!verify_edge(from, to)) {
                add_edge(from, to);
            }
        }
    }
}

void lock_after(uint64_t tid, uint64_t lockaddr) {
    
    int idx = 0;
    if (-1 == (idx = search_lock(lockaddr))) {
        
        int eidx = search_empty_lock(lockaddr);

        tg->locklist[eidx].id = tid;
        tg->locklist[eidx].lock_id = lockaddr;
        tg->lockidx ++;
    } else {
        struct source_type from;
        struct source_type to;

        from.id = tid;
        to.id = tg->locklist[idx].id;
        
        from.type = PROCESS;
        to.type = PROCESS;

        add_vertex(from);
        add_vertex(to);

        tg->locklist[idx].degree --;

        if (verify_edge(from, to)) {
            remove_edge(from, to);           
        }

        tg->locklist[idx].id = tid;
    }
   
}

void unlock_after(uint64_t tid, uint64_t lockaddr) {
    
    int idx = search_lock(lockaddr);
    if (tg->locklist[idx].degree == 0) {
        tg->locklist[idx].id = 0;
        tg->locklist[idx].lock_id = 0;
    }
}

void check_dead_lock(void) {

    int i = 0;
    deadlock = 0;
    for (i = 0; i < tg->num; i++) {
        if (deadlock == 1) {
            break;
        }

        search_for_cycle(i);
    }

    if (deadlock == 0) {
        printf("no deadlock\n");
    } 
}

static void* thread_routine(void *arg) {
    while (1) {

        sleep(5); // Simulate some work
        check_dead_lock();
    }
}

void start_check(){
    tg = (struct task_graph *)malloc(sizeof(struct task_graph));
    tg->num = 0;
    tg->lockidx = 0;

    pthread_t tid;

    pthread_create(&tid, NULL, thread_routine, NULL);
}

#if 1
// hooks for pthread_mutex_lock and pthread_mutex_unlock

typedef int(*pthread_mutex_lock_t)(pthread_mutex_t *);
pthread_mutex_lock_t pthread_mutex_lock_f = NULL;

typedef int(*pthread_mutex_unlock_t)(pthread_mutex_t *);
pthread_mutex_unlock_t pthread_mutex_unlock_f = NULL;

int pthread_mutex_lock(pthread_mutex_t *mutex) {

    pthread_t selfid = pthread_self();

    lock_before((uint64_t)selfid, (uint64_t)mutex);
    pthread_mutex_lock_f(mutex);
    lock_after((uint64_t)selfid, (uint64_t)mutex);

}                                                                                        

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
   
    pthread_mutex_unlock_f(mutex);

    pthread_t selfid = pthread_self();
    unlock_after((uint64_t)selfid, (uint64_t)mutex);
}

void init_hooks() {
    if (!pthread_mutex_lock_f) {
        pthread_mutex_lock_f = (pthread_mutex_lock_t)dlsym(RTLD_NEXT, "pthread_mutex_lock");
    }

    if (!pthread_mutex_unlock_f) {
        pthread_mutex_unlock_f = (pthread_mutex_unlock_t)dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    }
}  

#endif

#if 1
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex4 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex5 = PTHREAD_MUTEX_INITIALIZER;

void *thread1_cb(void *arg){
    printf("thread1: %ld\n", pthread_self());
    pthread_mutex_lock(&mutex1);

    sleep(1);

    pthread_mutex_lock(&mutex2);
    pthread_mutex_unlock(&mutex2);
    pthread_mutex_unlock(&mutex1);
}

void *thread2_cb(void *arg){
    printf("thread2: %ld\n", pthread_self());
    pthread_mutex_lock(&mutex2);

    sleep(1);

    pthread_mutex_lock(&mutex3);
    pthread_mutex_unlock(&mutex3);
    pthread_mutex_unlock(&mutex2);
}

void *thread3_cb(void *arg){
    printf("thread3: %ld\n", pthread_self());
    pthread_mutex_lock(&mutex3);

    sleep(1); 

    pthread_mutex_lock(&mutex4);
    pthread_mutex_unlock(&mutex4);
    pthread_mutex_unlock(&mutex3);
}

void *thread4_cb(void *arg){
    printf("thread4: %ld\n", pthread_self());
    pthread_mutex_lock(&mutex4);

    sleep(1); 

    pthread_mutex_lock(&mutex5);
    pthread_mutex_unlock(&mutex5);
    pthread_mutex_unlock(&mutex4);
}

void *thread5_cb(void *arg){
    printf("thread5: %ld\n", pthread_self());
    pthread_mutex_lock(&mutex5);

    sleep(1); 

    pthread_mutex_lock(&mutex1);
    pthread_mutex_unlock(&mutex1);
    pthread_mutex_unlock(&mutex5);
}

#endif

#if 0
int main(){
    init_hooks();
    pthread_t thread1, thread2, thread3, thread4;
    pthread_create(&thread1, NULL, thread1_cb, NULL);
    pthread_create(&thread2, NULL, thread2_cb, NULL);
    pthread_create(&thread3, NULL, thread3_cb, NULL);
    pthread_create(&thread4, NULL, thread4_cb, NULL);
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    pthread_join(thread3, NULL);
    pthread_join(thread4, NULL);
    return 0;
}

#endif

#if 1

int main () {
    init_hooks();
    start_check();

    pthread_t thread1, thread2, thread3, thread4, thread5;
    pthread_create(&thread1, NULL, thread1_cb, NULL);
    pthread_create(&thread2, NULL, thread2_cb, NULL);
    pthread_create(&thread3, NULL, thread3_cb, NULL);
    pthread_create(&thread4, NULL, thread4_cb, NULL);
    pthread_create(&thread5, NULL, thread5_cb, NULL);


    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);
    pthread_join(thread3, NULL);
    pthread_join(thread4, NULL);
    pthread_join(thread5, NULL);

    printf("complete!\n");
    return 0;
}

#endif
