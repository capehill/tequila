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
#include <gadgets/button.h>

#include <libraries/gadtools.h>
#include <libraries/keymap.h>

#include <stdio.h>

enum EObject {
    OID_Window,
    OID_AboutWindow,
    OID_ListBrowser,
    OID_InfoLayout,
    OID_Uptime,
    OID_Tasks,
    OID_Idle,
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
static struct ClassLibrary* ButtonBase;

static Class* WindowClass;
static Class* RequesterClass;
static Class* LayoutClass;
static Class* ButtonClass;

#define MAX_NODES 100

static struct ColumnInfo* columnInfo;
static struct List labelList;
static struct Node* nodes[MAX_NODES];

static void RemoveLabelNodes(void)
{
    while (IExec->RemHead(&labelList) != NULL) {
        //
    }
}

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

    ButtonBase = IIntuition->OpenClass("gadgets/button.gadget", version, &ButtonClass);
    if (!ButtonBase) {
        puts("Failed to open button.gadget");
        return FALSE;
    }

    return TRUE;
}

static void CloseClasses()
{
    IIntuition->CloseClass(WindowBase);
    IIntuition->CloseClass(RequesterBase);
    IIntuition->CloseClass(LayoutBase);
    IIntuition->CloseClass(ButtonBase);
}

static char* GetApplicationName()
{
    #define maxPathLen 255

    static char pathBuffer[maxPathLen];

    if (IDOS->GetCliProgramName(pathBuffer, maxPathLen - 1)) {
        //printf("GetCliProgramName: '%s'\n", pathBuffer);
    } else {
        if (ctx.debugMode) {
            puts("Failed to get CLI program name, checking task node");
        }

        snprintf(pathBuffer, maxPathLen, "%s", ((struct Node *)ctx.mainTask)->ln_Name);
    }

    //printf("Application name: '%s'\n", pathBuffer);

    return pathBuffer;
}

static struct DiskObject* MyGetDiskObject()
{
    BPTR oldDir = IDOS->SetCurrentDir(IDOS->GetProgramDir());
    struct DiskObject* diskObject = IIcon->GetDiskObject(GetApplicationName());
    IDOS->SetCurrentDir(oldDir);

    return diskObject;
}

static void ShowAboutWindow()
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

static Object* CreateGui()
{
    columnInfo = IListBrowser->AllocLBColumnInfo(5,
                                                 LBCIA_Column, 0,
                                                 LBCIA_Title, "Task",
                                                 LBCIA_Weight, 60,
                                                 LBCIA_Column, 1,
                                                 LBCIA_Title, "CPU %",
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 2,
                                                 LBCIA_Title, "Priority",
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 3,
                                                 LBCIA_Title, "Stack %",
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 4,
                                                 LBCIA_Title, "PID",
                                                 LBCIA_Weight, 10,
                                                 TAG_DONE);

    if (!columnInfo) {
        puts("Failed to allocate listbrowser column info");
        //return NULL;
    }

    for (int i = 0; i < MAX_NODES; i++) {
        nodes[i] = IListBrowser->AllocListBrowserNode(5, TAG_DONE);
        if (!nodes[i]) {
            printf("Failed to allocate listbrowser node %d\n", i);
        }
    }

    IExec->NewList(&labelList);

    static const char* const listBrowserHelp = "Task - task or process name\n"
        "CPU % - how much CPU task is using\n"
        "Priority - higher priority tasks get more CPU time\n"
        "Stack % - how much stack task is using\n"
        "PID - process ID. Plain tasks don't have PID";

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
        //WA_IDCMP, IDCMP_RAWKEY,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_Icon, MyGetDiskObject(),
        WINDOW_AppPort, port, // Iconification needs it
        WINDOW_GadgetHelp, TRUE,
        WINDOW_NewMenu, menus,
        WINDOW_Layout, IIntuition->NewObject(LayoutClass, NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,

            LAYOUT_AddChild, objects[OID_InfoLayout] = IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_Label, "Information",
                LAYOUT_BevelStyle, BVS_GROUP,

                LAYOUT_AddChild, objects[OID_Idle] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, "Idle --.--%",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_Tasks] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, "Tasks ---",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_Uptime] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, "Uptime --:--:--",
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                TAG_DONE), // horizontal layout.gadget
            CHILD_WeightedHeight, 10,

            LAYOUT_AddChild, objects[OID_ListBrowser] = IIntuition->NewObject(ListBrowserClass, NULL,
                GA_ReadOnly, TRUE,
                GA_HintInfo, listBrowserHelp,
                GA_ID, GID_ListBrowser,
                LISTBROWSER_ColumnInfo, columnInfo,
                LISTBROWSER_ColumnTitles, TRUE,
                LISTBROWSER_Labels, &labelList,
                LISTBROWSER_Striping, TRUE,
                TAG_DONE),
            CHILD_MinWidth, 400,
            CHILD_MinHeight, 200,
            CHILD_WeightedHeight, 90,

            TAG_DONE), // vertical layout.gadget
        TAG_DONE); // window.class
}

static void HandleGadgets(int id)
{
    printf("Gadget %d\n", id);
}

static void HandleIconify(void)
{
    window = NULL;
    IIntuition->IDoMethod(objects[OID_Window], WM_ICONIFY);
}

static void HandleUniconify(void)
{
    window = (struct Window *)IIntuition->IDoMethod(objects[OID_Window], WM_OPEN);
}

static BOOL HandleMenupick(uint16 menuNumber)
{
    struct MenuItem* item = IIntuition->ItemAddress(window->MenuStrip, menuNumber);

    if (item) {
        const EMenu id = (EMenu)GTMENUITEM_USERDATA(item);
        //printf("menu %x, menu num %d, item num %d, userdata %d\n", menuNumber, MENUNUM(menuNumber), ITEMNUM(menuNumber), (EMenu)GTMENUITEM_USERDATA(item));
        switch (id) {
            case MID_Iconify: HandleIconify(); break;
            case MID_About: ShowAboutWindow(); break;
            case MID_Quit: return FALSE;
        }
    }

    return TRUE;
}

static void UpdateDisplay(void)
{
    const size_t unique = PrepareResults();
    const size_t max = unique > MAX_NODES ? MAX_NODES : unique;

    //const float usage = getLoad(unique);

    static char tasksString[16];
    static char idleString[16];
    static char uptimeString[64];

    snprintf(tasksString, sizeof(tasksString), "Tasks %u", GetTotalTaskCount());
    snprintf(idleString, sizeof(idleString), "Idle %3.2f%%", GetIdleCpu(unique));
    snprintf(uptimeString, sizeof(uptimeString), "Uptime %s", GetUptimeString());

    IIntuition->SetAttrs(objects[OID_Idle],
                         GA_Text, idleString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_Tasks],
                         GA_Text, tasksString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_Uptime],
                         GA_Text, uptimeString,
                         TAG_DONE);

/*
    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_Idle], window, NULL,
                                      GA_Text, idleString,
                                      TAG_DONE);

    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_Tasks], window, NULL,
                                      GA_Text, taskString,
                                      TAG_DONE);

    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_Uptime], window, NULL,
                                      GA_Text, uptimeString,
                                      TAG_DONE);
*/
    IIntuition->RefreshGList((struct Gadget *)objects[OID_InfoLayout], window, NULL, -1);

    IIntuition->SetAttrs(objects[OID_ListBrowser],
                         LISTBROWSER_Labels, NULL,
                         TAG_DONE);

    RemoveLabelNodes();

    for (size_t i = 0; i < max; i++) {
        const float cpu = 100.0f * ctx.sampleInfo[i].count / (ctx.samples * ctx.interval);
        static char cpuBuffer[10];
        static char stackBuffer[10];
        const int32 priorityBuffer = ctx.sampleInfo[i].priority;
        const int32 pidBuffer = ctx.sampleInfo[i].pid;

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
                                              LBNA_Column, 4,
                                                LBNCA_CopyInteger, TRUE,
                                                LBNCA_Integer, &pidBuffer,
                                              TAG_DONE);

        IExec->AddTail(&labelList, nodes[i]);
    }

    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_ListBrowser], window, NULL,
                                      LISTBROWSER_Labels, labelList,
                                      TAG_DONE);
}

static BOOL HandleRawKey(const int16 code)
{
    //printf("code %d\n", code);
    return (code != RAWKEY_ESC);
}

static void HandleEvents(void)
{
    uint32 signal = 0;
    IIntuition->GetAttr(WINDOW_SigMask, objects[OID_Window], &signal);

    const uint32 timerSignal = 1L << ctx.timerSignal;

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
                        HandleGadgets(result & WMHI_GADGETMASK);
                        break;
                    case WMHI_ICONIFY:
                        HandleIconify();
                        break;
                    case WMHI_UNICONIFY:
                        HandleUniconify();
                        break;
                    case WMHI_MENUPICK:
                        running = HandleMenupick(result & WMHI_MENUMASK);
                        break;
                    case WMHI_RAWKEY:
                        running = HandleRawKey(code);
                        break;
                }
            }
        }

        if ((wait & timerSignal) && window) {
            MyClock start, finish;
            ITimer->ReadEClock(&start.un.clockVal);
            UpdateDisplay();
            ITimer->ReadEClock(&finish.un.clockVal);

            if (ctx.debugMode) {
                const uint64 duration = finish.un.ticks - start.un.ticks;

                printf("Display update %f ms\n", 0.001f * TicksToMicros(duration));
            }
        }
    }
}

void GuiLoop(void)
{
    if (OpenClasses()) {
        // TODO: port declaration
    	port = IExec->AllocSysObjectTags(ASOT_PORT,
    		ASOPORT_Name, "tequila_app_port",
    		TAG_DONE);

        if (!port) {
            puts("Failed to open msg port");
        }

        objects[OID_Window] = CreateGui();

        if (objects[OID_Window]) {
            if ((window = (struct Window *)IIntuition->IDoMethod(objects[OID_Window], WM_OPEN))) {
                HandleEvents();
            } else {
                puts("Failed to open window");
            }

            IIntuition->DisposeObject(objects[OID_Window]);
        } else {
            puts("Failed to create window");
        }

        IExec->FreeSysObject(ASOT_PORT, port);

        RemoveLabelNodes();

        for (int i = 0; i < MAX_NODES; i++) {
            IListBrowser->FreeListBrowserNode(nodes[i]);
        }

        IListBrowser->FreeLBColumnInfo(columnInfo);
    }

    ctx.running = FALSE;

    CloseClasses();
}

