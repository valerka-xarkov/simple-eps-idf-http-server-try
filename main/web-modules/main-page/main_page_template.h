typedef struct
{
    char *title;
    char *date;
} main_page_template_post_t;

typedef struct
{
    char *name;
    char *surname;
    int age;
    char *test1;
    char *test2;
    int posts_count;
    main_page_template_post_t *posts;
} main_page_template_context_t;

typedef void (*template_data_callback)(void *context, char *template_part);
void get_main_template(main_page_template_context_t *data, void *cb_context, template_data_callback cb);
