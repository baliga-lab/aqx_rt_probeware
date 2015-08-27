#pragma once
#ifndef __SIMPLE_TEMPLATES_H__
#define __SIMPLE_TEMPLATES_H__


/*
 * A simple hash based string template library. Templates look a little
 * like jinja, with mustaches, and {{key}} occurrences are replaced with value.
 */
#define STEMP_MAX_KEY_LENGTH 31

struct stemp_htable_entry {
  char key[STEMP_MAX_KEY_LENGTH + 1];
  char *value;
  struct stemp_htable_entry *next; /* to resolve hash collisions */
};

struct stemp_dict {
  int num_entries;
  int size;
  struct stemp_htable_entry **entries;
};

extern struct stemp_dict *stemp_new_dict();
extern void stemp_free_dict(struct stemp_dict *);
extern const char *stemp_dict_put(struct stemp_dict *dict, const char *key, const char *value);
extern const char *stemp_dict_get(struct stemp_dict *dict, const char *key);

/*
 * Replace template variables using the dictionary
 */
extern char *stemp_apply_template(const char *tpl_str, struct stemp_dict *dict);

#endif /* __SIMPLE_TEMPLATES_H__ */
