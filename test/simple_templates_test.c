#include <stdlib.h>

#include "chibi.h"
#include "../simple_templates.h"

CHIBI_TEST(Test_new_dict)
{
  struct stemp_dict *dict = stemp_new_dict();
  chibi_assert_not_null(dict);
  chibi_assert_eq_int(0, dict->num_entries);
  chibi_assert(dict->size > 0);
  stemp_free_dict(dict);
}

CHIBI_TEST(Test_put_get)
{
  struct stemp_dict *dict = stemp_new_dict();
  stemp_dict_put(dict, "key", "value");
  chibi_assert_eq_cstr("value", stemp_dict_get(dict, "key"));
  stemp_free_dict(dict);
}

CHIBI_TEST(Test_apply_template_nochange)
{
  char *result;
  struct stemp_dict *dict = stemp_new_dict();
  result = stemp_apply_template("nochange", dict);
  chibi_assert_eq_cstr("nochange", result);

  if (dict) stemp_free_dict(dict);
  if (result) free(result);
}

CHIBI_TEST(Test_apply_template_simple_replace)
{
  char *result;
  struct stemp_dict *dict = stemp_new_dict();
  stemp_dict_put(dict, "nochange", "change");
  result = stemp_apply_template("we want to {{nochange}} here", dict);
  chibi_assert_eq_cstr("we want to change here", result);

  if (dict) stemp_free_dict(dict);
  if (result) free(result);
}

int main(int argc, char **argv)
{
  chibi_suite *suite = chibi_suite_new();
  chibi_summary_data summary;
  chibi_suite_add_test(suite, Test_new_dict);
  chibi_suite_add_test(suite, Test_put_get);
  chibi_suite_add_test(suite, Test_apply_template_nochange);
  chibi_suite_add_test(suite, Test_apply_template_simple_replace);

  chibi_suite_run_tap(suite, &summary);
  chibi_suite_delete(suite);
  return summary.num_failures;
}
