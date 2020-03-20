#ifndef PCMANFM_PRIVATE_H
#define PCMANFM_PRIVATE_H

#ifdef ENABLE_NLS
#include <libintl.h>
#undef _
#define _(String) dgettext(GETTEXT_PACKAGE, String)

#ifdef gettext_noop
#define N_(String) gettext_noop(String)
#else
#define N_(String) (String)
#endif

#else
#define _(String)  (String)
#define N_(String) (String)
#endif

#endif
