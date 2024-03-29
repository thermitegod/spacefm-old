/* $Id: exo-string.c 47 2006-01-30 02:32:10Z pcmanx $ */
/*-
 * Copyright (c) 2004 os-cillation e.K.
 *
 * Written by Benedikt Meurer <benny@xfce.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "exo-string.h"

/**
 * exo_str_elide_underscores:
 * @text : A zero terminated string.
 *
 * Returns a copy of @text with all mnemonic underscores
 * stripped off.
 *
 * Returns: A copy of @text without underscores. The returned string
 *          must be freed when no longer required.
 **/
char* exo_str_elide_underscores(const char* text)
{
    const char* s;
    gboolean last_underscore = FALSE;
    char* result;
    char* t;

    g_return_val_if_fail(text != NULL, NULL);

    result = g_malloc(strlen(text) + 1);

    for (s = text, t = result; *s != '\0'; ++s)
        if (!last_underscore && *s == '_')
        {
            last_underscore = TRUE;
        }
        else
        {
            last_underscore = FALSE;
            *t++ = *s;
        }

    *t = '\0';

    return result;
}

/**
 * exo_str_is_equal:
 * @a : A pointer to first string or %NULL.
 * @b : A pointer to second string or %NULL.
 *
 * %NULL-safe string comparison. Returns %TRUE if both @a and @b are
 * %NULL or if @a and @b refer to valid strings which are equal.
 *
 * You should always prefer this function over strcmp().
 *
 * Returns: %TRUE if @a equals @b, else %FALSE.
 **/
gboolean exo_str_is_equal(const char* a, const char* b)
{
    if (a == NULL || b == NULL)
        return (a == b);

    while (*a == *b++)
        if (*a++ == '\0')
            return TRUE;

    return FALSE;
}

/**
 * exo_strndupv:
 * @strv  : String vector to duplicate.
 * @num   : Number of strings in @strv to
 *          duplicate.
 *
 * Creates a new string vector containing the
 * first @n elements of @strv.
 *
 * Return value: The new string vector. Should be
 *               freed using g_strfreev() when no
 *               longer needed.
 **/
char** exo_strndupv(char** strv, uint num)
{
    char** result;
    uint i;

    /* return null when there is nothing to copy */
    if (G_UNLIKELY(strv == NULL || num == 0))
        return NULL;

    /* duplicate the first @num string */
    result = g_new(gchar*, num + 1);
    for (i = 0; i < num && strv[i] != NULL; i++)
        result[i] = g_strdup(strv[i]);
    result[i] = NULL;

    /* resize the string if we allocated too much space */
    if (G_UNLIKELY(num > i))
        result = g_renew(gchar*, result, i + 1);

    return result;
}
