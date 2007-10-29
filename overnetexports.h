int overnet_republishnow(KadEngine *pKE, kobject *pko, time_t *pstoptime, int nthreads);
#ifndef OLD_SEARCH_ONLY
void *Overnet_find2(KadEngine *pKE, int128 targethash, int dosearch, unsigned char *psfilter, int sfilterlen, time_t *pstoptime, KadCfind_params *pfpar);
#endif
void *Overnet_find(KadEngine *pKE, int128 targethash, int dosearch, unsigned char *psfilter, int sfilterlen, time_t *pstoptime, int max_hits, int nthreads);
int overnet_republishnow(KadEngine *pKE, kobject *pko, time_t *pstoptime, int nthreads);
