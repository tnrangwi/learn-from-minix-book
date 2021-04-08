#ifndef ENV_H

const char *env_get(const char *);
int env_put(const char *);

void env_dump();

#else
#endif /* ENV_H */
