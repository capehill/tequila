#include <proto/dos.h>
#include <proto/locale.h>

#include <stdio.h>

#define CATCOMP_CODE
#define CATCOMP_BLOCK
#include "locale_generated.h"

static struct Locale* locale;
static struct Catalog* catalog;
static struct LocaleInfo localeInfo;

BOOL LocaleInit(void)
{
    if (ILocale) {
        localeInfo.li_ILocale = ILocale;

        locale = ILocale->OpenLocale(NULL);
        if (locale) {
            //printf("%s\n", locale->loc_LanguageName);
            catalog = ILocale->OpenCatalog(NULL, "tequila.catalog",
                                           TAG_DONE);
            localeInfo.li_Catalog = catalog;
            return TRUE;
        } else {
            puts("Failed to open locale");
        }
    }

    return FALSE;
}

void LocaleQuit(void)
{
    if (ILocale) {
        if (catalog) {
            ILocale->CloseCatalog(catalog);
            catalog = NULL;
        }

        if (locale) {
            ILocale->CloseLocale(locale);
            locale = NULL;
        }
    }
}

CONST_STRPTR GetString(LONG stringNum)
{
    CONST_STRPTR str = GetStringGenerated(&localeInfo, stringNum);
    if (str == NULL) {
        printf("Locale string num %ld missing\n", stringNum);
        return "missing";
    }

    return str;
}

