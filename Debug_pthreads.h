#define pthread_mutex_lock(a) Debug_pthread_mutex_lock((a), __FILE__, __LINE__)
int Debug_pthread_mutex_lock(pthread_mutex_t *m, char *file, int line);
#define pthread_mutex_unlock(a) Debug_pthread_mutex_unlock((a), __FILE__, __LINE__)
int Debug_pthread_mutex_unlock(pthread_mutex_t *m, char *file, int line);
#define pthread_mutex_init(a, b) Debug_pthread_mutex_init((a), (b), __FILE__, __LINE__)
int Debug_pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr, char *file, int line);
