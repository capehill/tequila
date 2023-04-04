#include "gui.h"
#include "version.h"
#include "profiler.h"
#include "common.h"

#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/listbrowser.h>

#include <classes/requester.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <libraries/gadtools.h>

#include <stdio.h>

enum EObject {
    OID_Window,
    OID_AboutWindow,
    OID_ListBrowser,
    OID_Count // KEEP LAST
};

enum EGadget {
    GID_ListBrowser
};

typedef enum EMenu {
    MID_Iconify = 1,
    MID_About,
    MID_Quit
} EMenu;

static struct NewMenu menus[] = {
    { NM_TITLE, "Tequila", NULL, 0, 0, NULL },
    { NM_ITEM, "Iconify", "I", 0, 0, (APTR)MID_Iconify },
    { NM_ITEM, "About...", "?", 0, 0, (APTR)MID_About },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)MID_Quit },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static Object* objects[OID_Count];
static struct Window* window;
static struct MsgPort* port;

static struct ClassLibrary* WindowBase;
static struct ClassLibrary* RequesterBase;
static struct ClassLibrary* LayoutBase;

static Class* WindowClass;
static Class* RequesterClass;
static Class* LayoutClass;

#define MAX_NODES 100

static struct ColumnInfo* columnInfo;
static struct List labelList;
static struct Node* nodes[MAX_NODES];

static BOOL OpenClasses()
{
    const int version = 53;

    WindowBase = IIntuition->OpenClass("window.class", version, &WindowClass);
    if (!WindowBase) {
        puts("Failed to open window.class");
        return FALSE;
    }

    RequesterBase = IIntuition->OpenClass("requester.class", version, &RequesterClass);
    if (!RequesterBase) {
        puts("Failed to open requester.class");
        return FALSE;
    }

    LayoutBase = IIntuition->OpenClass("gadgets/layout.gadget", version, &LayoutClass);
    if (!LayoutBase) {
        puts("Failed to open layout.gadget");
        return FALSE;
    }

    ListBrowserBase = (struct Library *)IIntuition->OpenClass("gadgets/listbrowser.gadget", version, &ListBrowserClass);
    if (!ListBrowserBase) {
        return FALSE;
        puts("Failed to open listbrowser.gadget");
    }

    return TRUE;
}

static void CloseClasses()
{
    IIntuition->CloseClass(WindowBase);
    IIntuition->CloseClass(RequesterBase);
    IIntuition->CloseClass(LayoutBase);
    IIntuition->CloseClass((struct ClassLibrary *)ListBrowserBase);
}

static char* getApplicationName()
{
    #define maxPathLen 255

    static char pathBuffer[maxPathLen];

    if (IDOS->GetCliProgramName(pathBuffer, maxPathLen - 1)) {
        //printf("GetCliProgramName: '%s'\n", pathBuffer);
    } else {
        puts("Failed to get CLI program name, checking task node");

        struct Task* me = IExec->FindTask(NULL);
        snprintf(pathBuffer, maxPathLen, "%s", ((struct Node *)me)->ln_Name);
    }

    //printf("Application name: '%s'\n", pathBuffer);

    return pathBuffer;
}

static struct DiskObject* getDiskObject()
{
    BPTR oldDir = IDOS->SetCurrentDir(IDOS->GetProgramDir());
    struct DiskObject* diskObject = IIcon->GetDiskObject(getApplicationName());
    IDOS->SetCurrentDir(oldDir);

    return diskObject;
}

static void show_about_window()
{
    objects[OID_AboutWindow] = IIntuition->NewObject(RequesterClass, NULL,
        REQ_TitleText, "About Tequila",
        REQ_BodyText, VERSION_STRING DATE_STRING,
        REQ_GadgetText, "_Ok",
        REQ_Image, REQIMAGE_INFO,
        TAG_DONE);

    if (objects[OID_AboutWindow]) {
        IIntuition->SetAttrs(objects[OID_Window], WA_BusyPointer, TRUE, TAG_DONE);
        IIntuition->IDoMethod(objects[OID_AboutWindow], RM_OPENREQ, NULL, window, NULL, TAG_DONE);
        IIntuition->SetAttrs(objects[OID_Window], WA_BusyPointer, FALSE, TAG_DONE);
        IIntuition->DisposeObject(objects[OID_AboutWindow]);
        objects[OID_AboutWindow] = NULL;
    }
}

static Object* create_gui()
{
    columnInfo = IListBrowser->AllocLBColumnInfo(4,
                                                 LBCIA_Column, 0,
                                                 LBCIA_Title, "Task",
                                                 LBCIA_Weight, 70,
                                                 LBCIA_Column, 1,
                                                 LBCIA_Title, "CPU %",
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 2,
                                                 LBCIA_Title, "Priority",
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 3,
                                                 LBCIA_Title, "Stack %",
                                                 LBCIA_Weight, 10,
                                                 TAG_DONE);

    if (!columnInfo) {
        puts("Failed to allocate listbrowser column info");
        //return NULL;
    }

    for (int i = 0; i < MAX_NODES; i++) {
        nodes[i] = IListBrowser->AllocListBrowserNode(4, TAG_DONE);
        if (!nodes[i]) {
            printf("Failed to allocate listbrowser node %d\n", i);
        }
    }

    IExec->NewList(&labelList);

    return IIntuition->NewObject(WindowClass, NULL,
        WA_ScreenTitle, VERSION_STRING DATE_STRING,
        WA_Title, VERSION_STRING,
        WA_Activate, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Width, 640,
        WA_Height, 480,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_Icon, getDiskObject(),
        WINDOW_AppPort, port, // Iconification needs it
        WINDOW_GadgetHelp, TRUE,
        WINDOW_NewMenu, menus,
        WINDOW_Layout, IIntuition->NewObject(LayoutClass, NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,

#if 0
            LAYOUT_AddChild, IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_Label, "Information",
                LAYOUT_BevelStyle, BVS_GROUP,
                TAG_DONE), // vertical layout.gadget
#endif

            LAYOUT_AddChild, objects[OID_ListBrowser] = IIntuition->NewObject(ListBrowserClass, NULL,
                GA_ReadOnly, TRUE,
                GA_HintInfo, "TODO",
                LISTBROWSER_ColumnInfo, columnInfo,
                LISTBROWSER_ColumnTitles, TRUE,
                LISTBROWSER_Labels, &labelList,
                LISTBROWSER_Striping, TRUE,
                TAG_DONE),
            CHILD_MinWidth, 400,
            CHILD_MinHeight, 200,

            TAG_DONE), // vertical layout.gadget
        TAG_DONE); // window.class
}

static void handle_gadgets(int id)
{
    printf("Gadget %d\n", id);
}

static void handle_iconify(void)
{
    window = NULL;
    IIntuition->IDoMethod(objects[OID_Window], WM_ICONIFY);
}

static void handle_uniconify(void)
{
    window = (struct Window *)IIntuition->IDoMethod(objects[OID_Window], WM_OPEN);
}

static BOOL handle_menupick(uint16 menuNumber)
{
    struct MenuItem* item = IIntuition->ItemAddress(window->MenuStrip, menuNumber);

    if (item) {
        const EMenu id = (EMenu)GTMENUITEM_USERDATA(item);
        //printf("menu %x, menu num %d, item num %d, userdata %d\n", menuNumber, MENUNUM(menuNumber), ITEMNUM(menuNumber), (EMenu)GTMENUITEM_USERDATA(item));
        switch (id) {
            case MID_Iconify: handle_iconify(); break;
            case MID_About: show_about_window(); break;
            case MID_Quit: return FALSE;
        }
    }

    return TRUE;
}

static void updateDisplay(void)
{
    const size_t unique = prepareResults();
    const size_t max = unique > MAX_NODES ? MAX_NODES : unique;

    //const float usage = getLoad(unique);

    IIntuition->SetAttrs(objects[OID_ListBrowser], LISTBROWSER_Labels, NULL, TAG_DONE);

    while (IExec->RemHead(&labelList) != NULL) {
        //
    }

    for (size_t i = 0; i < max; i++) {
        const float cpu = 100.0f * ctx.sampleInfo[i].count / (ctx.samples * ctx.interval);
        static char cpuBuffer[10];
        static char stackBuffer[10];
        const int32 priorityBuffer = ctx.sampleInfo[i].priority;

        snprintf(cpuBuffer, sizeof(cpuBuffer), "%3.2f", cpu);
        snprintf(stackBuffer, sizeof(stackBuffer), "%3.2f", ctx.sampleInfo[i].stackUsage);

        IListBrowser->SetListBrowserNodeAttrs(nodes[i],
                                              LBNA_Column, 0,
                                                LBNCA_CopyText, TRUE,
                                                LBNCA_Text, ctx.sampleInfo[i].nameBuffer,
                                              LBNA_Column, 1,
                                                LBNCA_CopyText, TRUE,
                                                LBNCA_Text, cpuBuffer,
                                              LBNA_Column, 2,
                                                LBNCA_CopyInteger, TRUE,
                                                LBNCA_Integer, &priorityBuffer,
                                              LBNA_Column, 3,
                                                LBNCA_CopyText, TRUE,
                                                LBNCA_Text, stackBuffer,
                                              TAG_DONE);

        IExec->AddTail(&labelList, nodes[i]);
    }

    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_ListBrowser], window, NULL,
                                      LISTBROWSER_Labels, labelList,
                                      TAG_DONE);
}

static void handle_events(void)
{
    uint32 signal = 0;
    IIntuition->GetAttr(WINDOW_SigMask, objects[OID_Window], &signal);

    const uint32 timerSignal = 1L << ctx.mainSig;

    BOOL running = TRUE;

    while (running) {
        uint32 wait = IExec->Wait(signal | timerSignal | SIGBREAKF_CTRL_C);

        if (wait & SIGBREAKF_CTRL_C) {
            puts("*** Break ***");
            running = FALSE;
        }

        if (wait & signal) {
            uint32 result;
            int16 code = 0;

            while ((result = IIntuition->IDoMethod(objects[OID_Window], WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        running = FALSE;
                        break;
                    case WMHI_GADGETUP:
                        handle_gadgets(result & WMHI_GADGETMASK);
                        break;
                    case WMHI_ICONIFY:
                        handle_iconify();
                        break;
                    case WMHI_UNICONIFY:
                        handle_uniconify();
                        break;
                    case WMHI_MENUPICK:
                        running = handle_menupick(result & WMHI_MENUMASK);
                        break;
                }
            }
        }

        if (wait & timerSignal) {
            updateDisplay();
        }
    }
}

void guiLoop(void)
{
    if (OpenClasses()) {
        // TODO: port declaration
    	port = IExec->AllocSysObjectTags(ASOT_PORT,
    		ASOPORT_Name, "tequila_app_port",
    		TAG_DONE);

        if (!port) {
            puts("Failed to open msg port");
        }

        objects[OID_Window] = create_gui();

        if (objects[OID_Window]) {
            if ((window = (struct Window *)IIntuition->IDoMethod(objects[OID_Window], WM_OPEN))) {
                handle_events();
            } else {
                puts("Failed to open window");
            }

            IIntuition->DisposeObject(objects[OID_Window]);
        } else {
            puts("Failed to create window");
        }

        IExec->FreeSysObject(ASOT_PORT, port);

        while (IExec->RemHead(&labelList) != NULL) {
            //puts("RemHead");
        }

        for (int i = 0; i < MAX_NODES; i++) {
            IListBrowser->FreeListBrowserNode(nodes[i]);
        }

        IListBrowser->FreeLBColumnInfo(columnInfo);
    }

    ctx.running = FALSE;

    CloseClasses();
}

