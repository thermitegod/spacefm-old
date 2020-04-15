
#include <glib-object.h>

/* WARNING: This code accesses GValues directly, which is UNSUPPORTED API.
 *          Do not access GValues directly in your code. Instead, use the
 *          g_value_get_*() functions
 */
#define g_marshal_value_peek_boolean(v) (v)->data[0].v_int
#define g_marshal_value_peek_int(v)     (v)->data[0].v_int
#define g_marshal_value_peek_enum(v)    (v)->data[0].v_long
#define g_marshal_value_peek_string(v)  (v)->data[0].v_pointer
#define g_marshal_value_peek_pointer(v) (v)->data[0].v_pointer
#define g_marshal_value_peek_object(v)  (v)->data[0].v_pointer

/* VOID:INT,INT (exo-marshal.list:1) */
void _exo_marshal_VOID__INT_INT(GClosure* closure, GValue* return_value, uint n_param_values,
                                const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef void (*GMarshalFunc_VOID__INT_INT)(gpointer data1, int arg_1, int arg_2, gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_VOID__INT_INT callback;

    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_VOID__INT_INT)(marshal_data ? marshal_data : cc->callback);

    callback(data1, g_marshal_value_peek_int(param_values + 1), g_marshal_value_peek_int(param_values + 2), data2);
}

/* VOID:OBJECT,OBJECT (exo-marshal.list:2) */
void _exo_marshal_VOID__OBJECT_OBJECT(GClosure* closure, GValue* return_value, uint n_param_values,
                                      const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT)(gpointer data1, gpointer arg1, gpointer arg2, gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_VOID__OBJECT_OBJECT callback;

    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_VOID__OBJECT_OBJECT)(marshal_data ? marshal_data : cc->callback);

    callback(data1,
             g_marshal_value_peek_object(param_values + 1),
             g_marshal_value_peek_object(param_values + 2),
             data2);
}

/* STRING:POINTER (exo-marshal.list:3) */
void _exo_marshal_STRING__POINTER(GClosure* closure, GValue* return_value, uint n_param_values,
                                  const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef char* (*GMarshalFunc_STRING__POINTER)(gpointer data1, gpointer arg1, gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_STRING__POINTER callback;
    char* v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 2);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_STRING__POINTER)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1, g_marshal_value_peek_pointer(param_values + 1), data2);

    g_value_take_string(return_value, v_return);
}

/* STRING:STRING,STRING (exo-marshal.list:4) */
void _exo_marshal_STRING__STRING_STRING(GClosure* closure, GValue* return_value, uint n_param_values,
                                        const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef char* (*GMarshalFunc_STRING__STRING_STRING)(gpointer data1, gpointer arg1, gpointer arg2, gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_STRING__STRING_STRING callback;
    char* v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_STRING__STRING_STRING)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1,
                        g_marshal_value_peek_string(param_values + 1),
                        g_marshal_value_peek_string(param_values + 2),
                        data2);

    g_value_take_string(return_value, v_return);
}

/* BOOLEAN:VOID (exo-marshal.list:5) */
void _exo_marshal_BOOLEAN__VOID(GClosure* closure, GValue* return_value, uint n_param_values,
                                const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef gboolean (*GMarshalFunc_BOOLEAN__VOID)(gpointer data1, gpointer data2);
    register GMarshalFunc_BOOLEAN__VOID callback;
    register GCClosure* cc = (GCClosure*)closure;
    register gpointer data1, data2;
    gboolean v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 1);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_BOOLEAN__VOID)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1, data2);

    g_value_set_boolean(return_value, v_return);
}

/* BOOLEAN:ENUM,INT (exo-marshal.list:6) */
void _exo_marshal_BOOLEAN__ENUM_INT(GClosure* closure, GValue* return_value, uint n_param_values,
                                    const GValue* param_values, gpointer invocation_hint, gpointer marshal_data)
{
    typedef gboolean (*GMarshalFunc_BOOLEAN__ENUM_INT)(gpointer data1, int arg1, int arg2, gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_BOOLEAN__ENUM_INT callback;
    gboolean v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 3);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_BOOLEAN__ENUM_INT)(marshal_data ? marshal_data : cc->callback);

    v_return =
        callback(data1, g_marshal_value_peek_enum(param_values + 1), g_marshal_value_peek_int(param_values + 2), data2);

    g_value_set_boolean(return_value, v_return);
}

/* BOOLEAN:INT,ENUM,BOOLEAN,ENUM,BOOLEAN (exo-marshal.list:7) */
void _exo_marshal_BOOLEAN__INT_ENUM_BOOLEAN_ENUM_BOOLEAN(GClosure* closure, GValue* return_value, uint n_param_values,
                                                         const GValue* param_values, gpointer invocation_hint,
                                                         gpointer marshal_data)
{
    typedef gboolean (*GMarshalFunc_BOOLEAN__INT_ENUM_BOOLEAN_ENUM_BOOLEAN)(gpointer data1,
                                                                            int arg1,
                                                                            int arg2,
                                                                            gboolean arg3,
                                                                            int arg4,
                                                                            gboolean arg5,
                                                                            gpointer data2);
    GCClosure* cc = (GCClosure*)closure;
    gpointer data1, data2;
    GMarshalFunc_BOOLEAN__INT_ENUM_BOOLEAN_ENUM_BOOLEAN callback;
    gboolean v_return;

    g_return_if_fail(return_value != NULL);
    g_return_if_fail(n_param_values == 6);

    if (G_CCLOSURE_SWAP_DATA(closure))
    {
        data1 = closure->data;
        data2 = g_value_peek_pointer(param_values + 0);
    }
    else
    {
        data1 = g_value_peek_pointer(param_values + 0);
        data2 = closure->data;
    }
    callback = (GMarshalFunc_BOOLEAN__INT_ENUM_BOOLEAN_ENUM_BOOLEAN)(marshal_data ? marshal_data : cc->callback);

    v_return = callback(data1,
                        g_marshal_value_peek_int(param_values + 1),
                        g_marshal_value_peek_enum(param_values + 2),
                        g_marshal_value_peek_boolean(param_values + 3),
                        g_marshal_value_peek_enum(param_values + 4),
                        g_marshal_value_peek_boolean(param_values + 5),
                        data2);

    g_value_set_boolean(return_value, v_return);
}
