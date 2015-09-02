#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simple_templates.h"
#define INITIAL_NUM_HASH_ENTRIES 100

unsigned long djb2_hash(const unsigned char *str)
{
  unsigned long hash = 5381;
  int c;

  while ((c = *str++)) hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
  return hash;
}

struct stemp_dict *stemp_new_dict()
{
  struct stemp_dict *result;
  size_t table_size = sizeof(struct stemp_htable_entry *) * INITIAL_NUM_HASH_ENTRIES;

  result = malloc(sizeof(struct stemp_dict));
  if (!result) return NULL;
  result->size = INITIAL_NUM_HASH_ENTRIES;
  result->num_entries = 0;
  result->entries = malloc(table_size);

  if (!result->entries) {
    free(result);
    return NULL;
  }
  /* initialize */
  memset(result->entries, 0, table_size);
  return result;
}

void stemp_free_dict(struct stemp_dict *dict)
{
  if (!dict) return;
  if (dict->entries) {
    struct stemp_htable_entry *slot, *cur, *next;
    int i;
    /* free each entry by freeing the values of each bucket first */
    for (i = 0; i < dict->size; i++) {
      slot = dict->entries[i];
      if (slot) {
        cur = slot;
        while (cur) {
          if (slot->value) free(slot->value);
          next = cur->next;
          free(cur);
          cur = next;
        }
      }
    }
    free(dict->entries);
    dict->entries = NULL;
    dict->num_entries = 0;
    dict->size = 0;
  }
  free(dict);
}

const char *stemp_dict_put(struct stemp_dict *dict, const char *key, const char *value)
{
  int slot;
  struct stemp_htable_entry *new_entry;

  /* NULL or long keys not allowed */
  if (!key || strlen(key) > STEMP_MAX_KEY_LENGTH) return NULL;

  /* no dictionary or table size 0, TODO should resize table */  
  if (!dict || dict->size == 0) return NULL;
  slot = djb2_hash((const unsigned char *) key) % dict->size;

  new_entry = malloc(sizeof(struct stemp_htable_entry));
  if (!new_entry) return NULL;

  memset(new_entry->key, 0, STEMP_MAX_KEY_LENGTH);
  new_entry->next = NULL;
  strncpy(new_entry->key, key, STEMP_MAX_KEY_LENGTH);

  /* reserve space for value and copy */
  new_entry->value = malloc(strlen(value));
  if (!new_entry->value) {
    free(new_entry);
    return NULL;
  }
  strcpy(new_entry->value, value);

  if (!dict->entries[slot]) dict->entries[slot] = new_entry;
  else {
    /* Append */
    struct stemp_htable_entry *cur = dict->entries[slot];
    while (cur->next) cur = cur->next;
    cur->next = cur;
  }
  dict->num_entries++;
  return key;
}

const char *stemp_dict_get(struct stemp_dict *dict, const char *key)
{
  int slot;
  struct stemp_htable_entry *cur;
  if (!key || !dict) return NULL;
  
  slot = djb2_hash((const unsigned char *) key) % dict->size;
  cur = dict->entries[slot];
  if (!cur) return NULL; /* no such entry */
  if (!strncmp(cur->key, key, STEMP_MAX_KEY_LENGTH)) return cur->value;
  while (cur->next) {
    cur = cur->next;
    if (!strncmp(cur->key, key, STEMP_MAX_KEY_LENGTH)) return cur->value;
  }
  return NULL;
}

static char *copy_char(char *dest, const char *src, int *i, int *j, int src_size,
                       int *dest_size)
{
  dest[(*j)++] = src[(*i)++];
  if (*j >= *dest_size) {
    /* TODO resize */
  }
  return dest;
}

static char *replace_expression(struct stemp_dict *dict, char *dest, const char *varname,
                                int *j, int *dest_size)
{
  const char *value = stemp_dict_get(dict, varname);
  if (value) {
    /* TODO: we currently will crash, if the replacement will result in
     a buffer overflow */
    int i;
    for (i = 0; i < strlen(value); i++) dest[(*j)++] = value[i];
  }
  return dest;
}


char *stemp_apply_template(const char *tpl_str, struct stemp_dict *dict)
{
  int input_size, output_size;
  char *output;

  if (!tpl_str) return NULL;
  input_size = strlen(tpl_str);
  output_size = input_size * 2;
  output = malloc(output_size);

  if (output) {
    int i = 0, j = 0;
    while (i < input_size) {
      if (tpl_str[i] == '{') {

        /* we found an expression, for now, all we support is
         simple key value lookups */
        if (i < input_size - 1 && tpl_str[i + 1] == '{') {
          int expr_start = i + 2, expr_end = i + 2, terminated = 0, error = 0;
          /* First find the limits of the expression variable */
          while (!terminated && !error) {
            if (expr_end + 1 < input_size) {
              if (tpl_str[expr_end] == '}' && tpl_str[expr_end + 1] == '}') terminated = 1;
            } else error = 1;
            expr_end++;
          }

          if (terminated) {
            char *expr;
            int expr_len;
            /* expr_end points to last '}' if successful */

            i = expr_end + 1; /* update input pointer */
            expr_end -= 2;
            expr_len = expr_end - expr_start + 1;
            expr = malloc(expr_len);
            memcpy(expr, &tpl_str[expr_start], expr_len);
            expr[expr_len] = 0;
            output = replace_expression(dict, output, expr, &j, &output_size);
            free(expr); /* don't need it anymore after replace */
          } else {
            /*
             * handle error: we can't produce a reliable result because
             * the input was garbage, so release the allocated resources and
             * return NULL
             */
            free(output);
            return NULL;
          }
          
        } else {
          /* Currently we ignore single occurrences of the '{' character */
          output = copy_char(output, tpl_str, &i, &j, input_size, &output_size);
        }
      } else {
        /* Default behaviour, carry on copying */
        output = copy_char(output, tpl_str, &i, &j, input_size, &output_size);
      }
    }
  }
  return output;
}
