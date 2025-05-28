#ifndef LOCALE_GENERATED_H
#define LOCALE_GENERATED_H


/****************************************************************************/


/* This file was created automatically by CatComp.
 * Do NOT edit by hand!
 */


#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif

#ifdef CATCOMP_ARRAY
#ifndef CATCOMP_NUMBERS
#define CATCOMP_NUMBERS
#endif
#ifndef CATCOMP_STRINGS
#define CATCOMP_STRINGS
#endif
#endif

#ifdef CATCOMP_BLOCK
#ifndef CATCOMP_STRINGS
#define CATCOMP_STRINGS
#endif
#endif


/****************************************************************************/


#ifdef CATCOMP_NUMBERS

#define MSG_TEQUILA_ICONIFY 0
#define MSG_TEQUILA_ABOUT 1
#define MSG_TEQUILA_QUIT 2
#define MSG_TEQUILA_ABOUT_REQ 3
#define MSG_TEQUILA_OK 4
#define MSG_COLUMN_TASK 5
#define MSG_COLUMN_CPU 6
#define MSG_COLUMN_PRIORITY 7
#define MSG_COLUMN_STACK 8
#define MSG_COLUMN_PID 9
#define MSG_TASK_DISPLAY_HINT 10
#define MSG_INFORMATION_LAYOUT_GAD 11
#define MSG_IDLE_INIT_VALUE 12
#define MSG_TASKS_INIT_VALUE 13
#define MSG_TASK_SWITCHES_INIT_VALUE 14
#define MSG_UPTIME_INIT_VALUE 15
#define MSG_IDLE 16
#define MSG_TASKS 17
#define MSG_UPTIME 18
#define MSG_THIS_TASK 19
#define MSG_UNKNOWN_TASK 20
#define MSG_TASK_SWITCHES 21
#define MSG_FORBID 22

#endif /* CATCOMP_NUMBERS */


/****************************************************************************/


#ifdef CATCOMP_STRINGS

#define MSG_TEQUILA_ICONIFY_STR "I|Iconify"
#define MSG_TEQUILA_ABOUT_STR "?|About"
#define MSG_TEQUILA_QUIT_STR "Q|Quit"
#define MSG_TEQUILA_ABOUT_REQ_STR "About Tequila"
#define MSG_TEQUILA_OK_STR "_Ok"
#define MSG_COLUMN_TASK_STR "Task"
#define MSG_COLUMN_CPU_STR "CPU"
#define MSG_COLUMN_PRIORITY_STR "Priority"
#define MSG_COLUMN_STACK_STR "Stack"
#define MSG_COLUMN_PID_STR "PID"
#define MSG_TASK_DISPLAY_HINT_STR "Task - task or process name\n CPU % - how much CPU task is using\nPriority - higher priority tasks get more CPU time\n Stack % - how much stack task is using\n PID - process ID. Plain tasks don't have PID"
#define MSG_INFORMATION_LAYOUT_GAD_STR "Information"
#define MSG_IDLE_INIT_VALUE_STR "Idle --.--"
#define MSG_TASKS_INIT_VALUE_STR "Tasks ---"
#define MSG_TASK_SWITCHES_INIT_VALUE_STR "Task switches / s ---"
#define MSG_UPTIME_INIT_VALUE_STR "Uptime --:--:--"
#define MSG_IDLE_STR "Idle"
#define MSG_TASKS_STR "Tasks"
#define MSG_UPTIME_STR "Uptime"
#define MSG_THIS_TASK_STR "this task"
#define MSG_UNKNOWN_TASK_STR "Unknown task"
#define MSG_TASK_SWITCHES_STR "Task switches / s"
#define MSG_FORBID_STR "Forbid"

#endif /* CATCOMP_STRINGS */


/****************************************************************************/


#ifdef CATCOMP_ARRAY

struct CatCompArrayType
{
    LONG         cca_ID;
    CONST_STRPTR cca_Str;
};

STATIC CONST struct CatCompArrayType CatCompArray[] =
{
    {MSG_TEQUILA_ICONIFY,(CONST_STRPTR)MSG_TEQUILA_ICONIFY_STR},
    {MSG_TEQUILA_ABOUT,(CONST_STRPTR)MSG_TEQUILA_ABOUT_STR},
    {MSG_TEQUILA_QUIT,(CONST_STRPTR)MSG_TEQUILA_QUIT_STR},
    {MSG_TEQUILA_ABOUT_REQ,(CONST_STRPTR)MSG_TEQUILA_ABOUT_REQ_STR},
    {MSG_TEQUILA_OK,(CONST_STRPTR)MSG_TEQUILA_OK_STR},
    {MSG_COLUMN_TASK,(CONST_STRPTR)MSG_COLUMN_TASK_STR},
    {MSG_COLUMN_CPU,(CONST_STRPTR)MSG_COLUMN_CPU_STR},
    {MSG_COLUMN_PRIORITY,(CONST_STRPTR)MSG_COLUMN_PRIORITY_STR},
    {MSG_COLUMN_STACK,(CONST_STRPTR)MSG_COLUMN_STACK_STR},
    {MSG_COLUMN_PID,(CONST_STRPTR)MSG_COLUMN_PID_STR},
    {MSG_TASK_DISPLAY_HINT,(CONST_STRPTR)MSG_TASK_DISPLAY_HINT_STR},
    {MSG_INFORMATION_LAYOUT_GAD,(CONST_STRPTR)MSG_INFORMATION_LAYOUT_GAD_STR},
    {MSG_IDLE_INIT_VALUE,(CONST_STRPTR)MSG_IDLE_INIT_VALUE_STR},
    {MSG_TASKS_INIT_VALUE,(CONST_STRPTR)MSG_TASKS_INIT_VALUE_STR},
    {MSG_TASK_SWITCHES_INIT_VALUE,(CONST_STRPTR)MSG_TASK_SWITCHES_INIT_VALUE_STR},
    {MSG_UPTIME_INIT_VALUE,(CONST_STRPTR)MSG_UPTIME_INIT_VALUE_STR},
    {MSG_IDLE,(CONST_STRPTR)MSG_IDLE_STR},
    {MSG_TASKS,(CONST_STRPTR)MSG_TASKS_STR},
    {MSG_UPTIME,(CONST_STRPTR)MSG_UPTIME_STR},
    {MSG_THIS_TASK,(CONST_STRPTR)MSG_THIS_TASK_STR},
    {MSG_UNKNOWN_TASK,(CONST_STRPTR)MSG_UNKNOWN_TASK_STR},
    {MSG_TASK_SWITCHES,(CONST_STRPTR)MSG_TASK_SWITCHES_STR},
    {MSG_FORBID,(CONST_STRPTR)MSG_FORBID_STR},
};

#endif /* CATCOMP_ARRAY */


/****************************************************************************/


#ifdef CATCOMP_BLOCK

STATIC CONST UBYTE CatCompBlock[] =
{
    "\x00\x00\x00\x00\x00\x0A"
    MSG_TEQUILA_ICONIFY_STR "\x00"
    "\x00\x00\x00\x01\x00\x08"
    MSG_TEQUILA_ABOUT_STR "\x00"
    "\x00\x00\x00\x02\x00\x08"
    MSG_TEQUILA_QUIT_STR "\x00\x00"
    "\x00\x00\x00\x03\x00\x0E"
    MSG_TEQUILA_ABOUT_REQ_STR "\x00"
    "\x00\x00\x00\x04\x00\x04"
    MSG_TEQUILA_OK_STR "\x00"
    "\x00\x00\x00\x05\x00\x06"
    MSG_COLUMN_TASK_STR "\x00\x00"
    "\x00\x00\x00\x06\x00\x04"
    MSG_COLUMN_CPU_STR "\x00"
    "\x00\x00\x00\x07\x00\x0A"
    MSG_COLUMN_PRIORITY_STR "\x00\x00"
    "\x00\x00\x00\x08\x00\x06"
    MSG_COLUMN_STACK_STR "\x00"
    "\x00\x00\x00\x09\x00\x04"
    MSG_COLUMN_PID_STR "\x00"
    "\x00\x00\x00\x0A\x00\xCA"
    MSG_TASK_DISPLAY_HINT_STR "\x00\x00"
    "\x00\x00\x00\x0B\x00\x0C"
    MSG_INFORMATION_LAYOUT_GAD_STR "\x00"
    "\x00\x00\x00\x0C\x00\x0C"
    MSG_IDLE_INIT_VALUE_STR "\x00\x00"
    "\x00\x00\x00\x0D\x00\x0A"
    MSG_TASKS_INIT_VALUE_STR "\x00"
    "\x00\x00\x00\x0E\x00\x16"
    MSG_TASK_SWITCHES_INIT_VALUE_STR "\x00"
    "\x00\x00\x00\x0F\x00\x10"
    MSG_UPTIME_INIT_VALUE_STR "\x00"
    "\x00\x00\x00\x10\x00\x06"
    MSG_IDLE_STR "\x00\x00"
    "\x00\x00\x00\x11\x00\x06"
    MSG_TASKS_STR "\x00"
    "\x00\x00\x00\x12\x00\x08"
    MSG_UPTIME_STR "\x00\x00"
    "\x00\x00\x00\x13\x00\x0A"
    MSG_THIS_TASK_STR "\x00"
    "\x00\x00\x00\x14\x00\x0E"
    MSG_UNKNOWN_TASK_STR "\x00\x00"
    "\x00\x00\x00\x15\x00\x12"
    MSG_TASK_SWITCHES_STR "\x00"
    "\x00\x00\x00\x16\x00\x08"
    MSG_FORBID_STR "\x00\x00"
};

#endif /* CATCOMP_BLOCK */


/****************************************************************************/


#ifdef CATCOMP_CODE

#ifndef PROTO_LOCALE_H
#define __NOLIBBASE__
#define __NOGLOBALIFACE__
#include <proto/locale.h>
#endif

struct LocaleInfo
{
#ifndef __amigaos4__
    struct Library     *li_LocaleBase;
#else
    struct LocaleIFace *li_ILocale;
#endif
    struct Catalog     *li_Catalog;
};


CONST_STRPTR GetStringGenerated(struct LocaleInfo *li, LONG stringNum);

CONST_STRPTR GetStringGenerated(struct LocaleInfo *li, LONG stringNum)
{
#ifndef __amigaos4__
    struct Library     *LocaleBase = li->li_LocaleBase;
#else
    struct LocaleIFace *ILocale    = li->li_ILocale;
#endif
    LONG         *l;
    UWORD        *w;
    CONST_STRPTR  builtIn;

    l = (LONG *)CatCompBlock;

    while (*l != stringNum)
    {
        w = (UWORD *)((ULONG)l + 4);
        l = (LONG *)((ULONG)l + (ULONG)*w + 6);
    }
    builtIn = (CONST_STRPTR)((ULONG)l + 6);

#ifndef __amigaos4__
    if (LocaleBase)
    {
        return GetCatalogStr(li->li_Catalog, stringNum, builtIn);
    }
#else
    if (ILocale)
    {
#ifdef __USE_INLINE__
        return GetCatalogStr(li->li_Catalog, stringNum, builtIn);
#else
        return ILocale->GetCatalogStr(li->li_Catalog, stringNum, builtIn);
#endif
    }
#endif
    return builtIn;
}


#endif /* CATCOMP_CODE */


/****************************************************************************/


#endif /* LOCALE_GENERATED_H */
