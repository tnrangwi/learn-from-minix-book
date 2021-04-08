#ifndef ENV_H

const char *env_get(const char *);
int env_put(const char *);
void env_dump();
int env_get_detail(const char *, char ***, const char **);

#else
#endif /* ENV_H */
