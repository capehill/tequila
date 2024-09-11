#include "gui.h"
#include "version.h"
#include "profiler.h"
#include "common.h"

#define CATCOMP_NUMBERS
#include "locale_generated.h"
#include "locale.h"

#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/listbrowser.h>
#include <proto/graphics.h>

#include <classes/requester.h>
#include <classes/window.h>
#include <intuition/menuclass.h>

#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/button.h>
#include <gadgets/space.h>

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
    OID_TaskSwitches,
    OID_Idle,
    OID_Forbid,
    OID_LoadAverage,
    OID_Space,
    OID_Count // KEEP LAST
};

enum EGadget {
    GID_ListBrowser,
    GID_Space
};

typedef enum EMenu {
    MID_Iconify = 1,
    MID_About,
    MID_Quit
} EMenu;

struct CustomRendering {
    struct RastPort rp;
    struct BitMap* bitmap;
    int width;
    int height;
    int columnWidth[5];
};

static struct CustomRendering cr;

static Object* objects[OID_Count];
static struct Window* window;

static struct ClassLibrary* WindowBase;
static struct ClassLibrary* RequesterBase;
static struct ClassLibrary* LayoutBase;
static struct ClassLibrary* ButtonBase;
static struct ClassLibrary* SpaceBase;

static Class* WindowClass;
static Class* RequesterClass;
static Class* LayoutClass;
static Class* ButtonClass;
static Class* SpaceClass;

#define MAX_NODES MAX_TASKS

static struct ColumnInfo* columnInfo;
static struct List* labelList;
static struct Node* nodes[MAX_NODES];

static void RemoveLabelNodes(void)
{
    while (IExec->RemHead(labelList) != NULL) {
        //
    }
}

static BOOL OpenClasses(void)
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

    SpaceBase = IIntuition->OpenClass("gadgets/space.gadget", version, &SpaceClass);
    if (!SpaceBase) {
        puts("Failed to open space.gadget");
        return FALSE;
    }

    return TRUE;
}

static void CloseClasses(void)
{
    IIntuition->CloseClass(WindowBase);
    IIntuition->CloseClass(RequesterBase);
    IIntuition->CloseClass(LayoutBase);
    IIntuition->CloseClass(ButtonBase);
    IIntuition->CloseClass(SpaceBase);
}

static char* GetApplicationName(void)
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

static struct DiskObject* MyGetDiskObject(void)
{
    BPTR oldDir = IDOS->SetCurrentDir(IDOS->GetProgramDir());
    struct DiskObject* diskObject = IIcon->GetDiskObject(GetApplicationName());
    IDOS->SetCurrentDir(oldDir);

    return diskObject;
}

static void ShowAboutWindow(void)
{
    objects[OID_AboutWindow] = IIntuition->NewObject(RequesterClass, NULL,
        REQ_TitleText, GetString(MSG_TEQUILA_ABOUT_REQ),
        REQ_BodyText, VERSION_STRING DATE_STRING,
        REQ_GadgetText, GetString(MSG_TEQUILA_OK),
        REQ_Image, REQIMAGE_INFO,
        REQ_TimeOutSecs, 10,
        TAG_DONE);

    if (objects[OID_AboutWindow]) {
        IIntuition->SetAttrs(objects[OID_Window], WA_BusyPointer, TRUE, TAG_DONE);
        IIntuition->IDoMethod(objects[OID_AboutWindow], RM_OPENREQ, NULL, window, NULL, TAG_DONE);
        IIntuition->SetAttrs(objects[OID_Window], WA_BusyPointer, FALSE, TAG_DONE);
        IIntuition->DisposeObject(objects[OID_AboutWindow]);
        objects[OID_AboutWindow] = NULL;
    }
}

static Object* CreateMenu(void)
{
    return IIntuition->NewObject(NULL, "menuclass",
        MA_Type, T_ROOT,
        MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
            MA_Type, T_MENU,
            MA_Label, "Tequila",
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_TEQUILA_ICONIFY),
                MA_ID, MID_Iconify,
                TAG_DONE),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_TEQUILA_ABOUT),
                MA_ID, MID_About,
                TAG_DONE),
            MA_AddChild, IIntuition->NewObject(NULL, "menuclass",
                MA_Type, T_ITEM,
                MA_Label, GetString(MSG_TEQUILA_QUIT),
                MA_ID, MID_Quit,
                TAG_DONE),
            TAG_DONE),
        TAG_DONE);
}

static int min(int a, int b)
{
    return (a < b) ? a : b;
}

static uint32 RenderHook(struct Hook* hook __attribute__((unused)), APTR space, struct gpRender* msg)
{
    if (msg->gpr_Redraw == GREDRAW_REDRAW && msg->gpr_RPort && window && cr.bitmap) {
        struct IBox box;

        if (IIntuition->GetAttr(SPACE_RenderBox, space, (uint32 *)&box)) {
            IGraphics->BltBitMapRastPort(cr.bitmap, 0, 0, msg->gpr_RPort,
                                         box.Left,
                                         box.Top,
                                         (WORD)min(box.Width, cr.width),
                                         (WORD)min(box.Height, cr.height),
                                         0xC0);
        }
    }

    return 0;
}

static void ResizeBitMap(int w, int h)
{
    if (w > cr.width || h > cr.height) {
        IGraphics->FreeBitMap(cr.bitmap);

        cr.bitmap = IGraphics->AllocBitMapTags((uint32)w, (uint32)h, 32,
                                               BMATags_PixelFormat, PIXF_A8R8G8B8,
                                               //BMATags_UserPrivate, TRUE,
                                               BMATags_Displayable, TRUE,
                                               TAG_DONE);

        if (cr.bitmap) {
            cr.width = (int)IGraphics->GetBitMapAttr(cr.bitmap, BMA_ACTUALWIDTH);
            cr.height = (int)IGraphics->GetBitMapAttr(cr.bitmap, BMA_HEIGHT);

            cr.rp.BitMap = cr.bitmap;
            //printf("New bitmap %d * %d\n", width, height);
        }
    }
}

static BOOL InitCustomRendering(void)
{
    IGraphics->InitRastPort(&cr.rp);

    ResizeBitMap(640, 480);

    if (!cr.bitmap) {
        puts("Failed to create bitmap");
        return FALSE;
    }

    char buffer[16];
    int len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_TASK));
    cr.columnWidth[0] = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

    len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_CPU));
    cr.columnWidth[1] = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

    len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_PRIORITY));
    cr.columnWidth[2] = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

    len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_STACK));
    cr.columnWidth[3] = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

    len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_PID));
    cr.columnWidth[4] = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

    return TRUE;
}

static BOOL InitListBrowserData(void)
{
    labelList = IExec->AllocSysObject(ASOT_LIST, TAG_DONE);

    if (!labelList) {
        puts("Failed to allocate label list");
        return FALSE;
    }

    columnInfo = IListBrowser->AllocLBColumnInfo(5,
                                                 LBCIA_Column, 0,
                                                 LBCIA_Title, GetString(MSG_COLUMN_TASK),
                                                 LBCIA_Weight, 60,
                                                 LBCIA_Column, 1,
                                                 LBCIA_Title, GetString(MSG_COLUMN_CPU),
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 2,
                                                 LBCIA_Title, GetString(MSG_COLUMN_PRIORITY),
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 3,
                                                 LBCIA_Title, GetString(MSG_COLUMN_STACK),
                                                 LBCIA_Weight, 10,
                                                 LBCIA_Column, 4,
                                                 LBCIA_Title, GetString(MSG_COLUMN_PID),
                                                 LBCIA_Weight, 10,
                                                 TAG_DONE);

    if (!columnInfo) {
        puts("Failed to allocate listbrowser column info");
        return FALSE;
    }

    for (int i = 0; i < MAX_NODES; i++) {
        nodes[i] = IListBrowser->AllocListBrowserNode(5, TAG_DONE);
        if (!nodes[i]) {
            printf("Failed to allocate listbrowser node %d\n", i);
            return FALSE;
        }
    }

    return TRUE;
}

static Object* CreateTaskDisplay(void)
{
    if (ctx.customRendering) {
        static struct Hook renderHook = {
            { 0, 0 },
            (HOOKFUNC)RenderHook, /* entry */
            NULL, /* subentry */
            NULL /* data */
        };

        if (!InitCustomRendering()) {
            return NULL;
        }

        return IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_AddChild, objects[OID_Space] = IIntuition->NewObject(SpaceClass, NULL,
                    GA_HintInfo, GetString(MSG_TASK_DISPLAY_HINT),
                    GA_ID, GID_Space,
                    SPACE_Transparent, TRUE,
                    SPACE_RenderHook, renderHook,
                    TAG_DONE),
                TAG_DONE);
    } else {
        if (!InitListBrowserData()) {
            return NULL;
        }

        objects[OID_ListBrowser] = IIntuition->NewObject(ListBrowserClass, NULL,
            GA_ReadOnly, TRUE,
            GA_HintInfo, GetString(MSG_TASK_DISPLAY_HINT),
            GA_ID, GID_ListBrowser,
            LISTBROWSER_ColumnInfo, columnInfo,
            LISTBROWSER_ColumnTitles, TRUE,
            LISTBROWSER_Labels, labelList,
            LISTBROWSER_Striping, TRUE,
            TAG_DONE);

        return objects[OID_ListBrowser];
   }
}

static Object* CreateGui(struct MsgPort* port)
{
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
        WA_IDCMP, IDCMP_RAWKEY | IDCMP_NEWSIZE,
        WA_MenuStrip, CreateMenu(),
        WA_SimpleRefresh, TRUE,
        WINDOW_Position, WPOS_CENTERMOUSE,
        WINDOW_IconifyGadget, TRUE,
        WINDOW_Icon, MyGetDiskObject(),
        WINDOW_AppPort, port, // Iconification needs it
        WINDOW_GadgetHelp, TRUE,
        WINDOW_Layout, IIntuition->NewObject(LayoutClass, NULL,
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,

            LAYOUT_AddChild, objects[OID_InfoLayout] = IIntuition->NewObject(LayoutClass, NULL,
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_Label, GetString(MSG_INFORMATION_LAYOUT_GAD),
                LAYOUT_BevelStyle, BVS_GROUP,

                LAYOUT_AddChild, objects[OID_Idle] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, GetString(MSG_IDLE_INIT_VALUE),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_Forbid] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, GetString(MSG_FORBID),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_Tasks] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, GetString(MSG_TASKS_INIT_VALUE),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_TaskSwitches] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, GetString(MSG_TASK_SWITCHES_INIT_VALUE),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_Uptime] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, GetString(MSG_UPTIME_INIT_VALUE),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                LAYOUT_AddChild, objects[OID_LoadAverage] = IIntuition->NewObject(ButtonClass, NULL,
                    GA_ReadOnly, TRUE,
                    GA_Text, "Load average 0.0 % 0.0 % 0.0 %", // TODO: GetString(MSG_LOAD_AVERAGE_INIT_VALUE),
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Transparent, TRUE,
                    TAG_DONE),

                TAG_DONE), // horizontal layout.gadget
            CHILD_WeightedHeight, 10,

            LAYOUT_AddChild, CreateTaskDisplay(),
            CHILD_MinWidth, 600,
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

static BOOL HandleMenupick(void)
{
    uint32 id = NO_MENU_ID;

    while (window && ((id = IIntuition->IDoMethod((Object *)window->MenuStrip, MM_NEXTSELECT, 0, id))) != NO_MENU_ID) {
        switch (id) {
            case MID_Iconify: HandleIconify(); break;
            case MID_About: ShowAboutWindow(); break;
            case MID_Quit: return FALSE;
        }
    }

    return TRUE;
}

static void UpdateBitMap(void)
{
    struct IBox box;

    if (IIntuition->GetAttr(SPACE_RenderBox, objects[OID_Space], (uint32 *)&box)) {
        if (box.Width > cr.width || box.Height > cr.height) {
            ResizeBitMap(box.Width, box.Height);
        }

        IGraphics->RectFillColor(&cr.rp,
                                 0,
                                 0,
                                 (uint32)box.Width - 1,
                                 (uint32)box.Height - 1,
                                 0xFF000000);

        IGraphics->SetRPAttrs(&cr.rp,
                              RPTAG_APenColor, 0xFF00FF00,
                              RPTAG_BPenColor, 0xFF000000,
                              RPTAG_DrMd, JAM2,
                              TAG_DONE);

        const int xOffset[5] = { 1,
                                 (int)(0.65f * box.Width), // Special adjustment, "Priority" column takes more space
                                 (int)(0.8f * box.Width),
                                 (int)(0.9f * box.Width),
                                 box.Width - 2 };

        static char buffer[NAME_LEN];

        WORD yOffset = (WORD)cr.rp.TxHeight;

        {
            /* Columns */
            int len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_TASK));

            IGraphics->Move(&cr.rp, (WORD)xOffset[0], yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_CPU));

            IGraphics->Move(&cr.rp, (WORD)(xOffset[1] - cr.columnWidth[1]), yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_PRIORITY));

            IGraphics->Move(&cr.rp, (WORD)(xOffset[2] - cr.columnWidth[2]), yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_STACK));

            IGraphics->Move(&cr.rp, (WORD)(xOffset[3] - cr.columnWidth[3]), yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), GetString(MSG_COLUMN_PID));

            IGraphics->Move(&cr.rp, (WORD)(xOffset[4] - cr.columnWidth[4]), yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);
        }

        /* Dynamic content */
        for (size_t i = 0; i < ctx.front->uniqueTasks; i++) {
            SampleInfo* si = &ctx.sampleInfo[i];
            const float cpu = 100.0f * (float)si->count / (float)ctx.totalSamples;

            yOffset += (WORD)cr.rp.TxHeight;

            int len = snprintf(buffer, sizeof(buffer), "%s", si->nameBuffer);

            IGraphics->Move(&cr.rp, (WORD)xOffset[0], yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), "%3.1f", cpu);
            WORD textLength = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

            IGraphics->Move(&cr.rp, (WORD)xOffset[1] - textLength, yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), "%d", si->priority);
            textLength = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

            IGraphics->Move(&cr.rp, (WORD)xOffset[2] - textLength, yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            len = snprintf(buffer, sizeof(buffer), "%3.1f", si->stackUsage);
            textLength = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

            IGraphics->Move(&cr.rp, (WORD)xOffset[3] - textLength, yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);

            if (si->pid > 0) {
                len = snprintf(buffer, sizeof(buffer), "%lu", si->pid);
            } else {
                len = snprintf(buffer, sizeof(buffer), "(task)");
            }

            textLength = IGraphics->TextLength(&cr.rp, buffer, (UWORD)len);

            IGraphics->Move(&cr.rp, (WORD)xOffset[4] - textLength, yOffset);
            IGraphics->Text(&cr.rp, buffer, (UWORD)len);
        }
    }

    IIntuition->RefreshGList((struct Gadget *)objects[OID_Space], window, NULL, 1);
}

static void UpdateListBrowser(void)
{
    IIntuition->SetAttrs(objects[OID_ListBrowser],
                         LISTBROWSER_Labels, NULL,
                         TAG_DONE);

    RemoveLabelNodes();

    for (size_t i = 0; i < ctx.front->uniqueTasks; i++) {
        SampleInfo* si = &ctx.sampleInfo[i];
        const float cpu = 100.0f * (float)si->count / (float)ctx.totalSamples;
        static char cpuBuffer[10];
        static char stackBuffer[10];
        static char pidBuffer[16];
        const int32 priorityBuffer = si->priority;

        snprintf(cpuBuffer, sizeof(cpuBuffer), "%3.1f", cpu);
        snprintf(stackBuffer, sizeof(stackBuffer), "%3.1f", si->stackUsage);
        if (si->pid > 0) {
            snprintf(pidBuffer, sizeof(pidBuffer), "%lu", si->pid);
        } else {
            snprintf(pidBuffer, sizeof(pidBuffer), "(task)");
        }

        IListBrowser->SetListBrowserNodeAttrs(nodes[i],
                                              LBNA_Column, 0,
                                                LBNCA_CopyText, TRUE,
                                                LBNCA_Text, si->nameBuffer,
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
                                                LBNCA_CopyText, TRUE,
                                                LBNCA_Text, pidBuffer,
                                              TAG_DONE);

        IExec->AddTail(labelList, nodes[i]);
    }

    IIntuition->RefreshSetGadgetAttrs((struct Gadget *)objects[OID_ListBrowser], window, NULL,
                                      LISTBROWSER_Labels, labelList,
                                      TAG_DONE);
}

static void UpdateDisplay(void)
{
    PrepareResults();

    static char idleString[16];
    static char forbidString[16];
    static char tasksString[16];
    static char taskSwitchesString[32];
    static char uptimeString[64];
    static char loadAverageString[32];

    snprintf(idleString, sizeof(idleString), "%s %3.1f%%", GetString(MSG_IDLE), GetIdleCpu());
    snprintf(forbidString, sizeof(forbidString), "%s %3.1f%%", GetString(MSG_FORBID), GetForbidCpu());
    snprintf(tasksString, sizeof(tasksString), "%s %u", GetString(MSG_TASKS), GetTotalTaskCount());
    snprintf(taskSwitchesString, sizeof(taskSwitchesString), "%s %lu", GetString(MSG_TASK_SWITCHES), ctx.taskSwitchesPerSecond);
    snprintf(uptimeString, sizeof(uptimeString), "%s %s", GetString(MSG_UPTIME), GetUptimeString());
    snprintf(loadAverageString, sizeof(loadAverageString), "Load average %3.1f%% %3.1f%% %3.1f%%",
             ctx.loadAverage1, ctx.loadAverage5, ctx.loadAverage15);

    IIntuition->SetAttrs(objects[OID_Idle],
                         GA_Text, idleString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_Forbid],
                         GA_Text, forbidString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_Tasks],
                         GA_Text, tasksString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_TaskSwitches],
                         GA_Text, taskSwitchesString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_Uptime],
                         GA_Text, uptimeString,
                         TAG_DONE);

    IIntuition->SetAttrs(objects[OID_LoadAverage],
                         GA_Text, loadAverageString,
                         TAG_DONE);

    IIntuition->RefreshGList((struct Gadget *)objects[OID_InfoLayout], window, NULL, -1);

    if (ctx.customRendering) {
        UpdateBitMap();
    } else {
        UpdateListBrowser();
    }
}

static BOOL HandleRawKey(const int16 code)
{
    //printf("code %d\n", code);
    return (code != RAWKEY_ESC);
}

static void HandleNewSize(void)
{
    //puts("new size");
    struct IBox box;

    if (IIntuition->GetAttr(SPACE_RenderBox, objects[OID_Space], (uint32*)&box)) {
        ResizeBitMap(box.Width, box.Height);
    }
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

        BOOL refresh = FALSE;

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
                        running = HandleMenupick();
                        break;
                    case WMHI_RAWKEY:
                        running = HandleRawKey(code);
                        break;
                    case WMHI_NEWSIZE:
                        HandleNewSize();
                        if (ctx.customRendering) {
                            refresh = TRUE;
                        }
                        break;
                }
            }
        }

        if (((int)(wait & timerSignal) | refresh) && window) {
            MyClock start, finish;
            ITimer->ReadEClock(&start.un.clockVal);
            UpdateDisplay();
            ITimer->ReadEClock(&finish.un.clockVal);

            if (ctx.debugMode) {
                const uint64 duration = finish.un.ticks - start.un.ticks;

                if (duration > ctx.longestDisplayUpdate) {
                    ctx.longestDisplayUpdate = duration;
                }

                printf("Display update %g us (longest %g us), longest interrupt %g us\n",
                       TicksToMicros(duration),
                       TicksToMicros(ctx.longestDisplayUpdate),
                       TicksToMicros(ctx.longestInterrupt));
            }
        }
    }
}

void GuiLoop(void)
{
    if (OpenClasses()) {
    	struct MsgPort* port = IExec->AllocSysObjectTags(ASOT_PORT,
    		ASOPORT_Name, "tequila_app_port",
    		TAG_DONE);

        if (!port) {
            puts("Failed to open msg port");
        }

        objects[OID_Window] = CreateGui(port);

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

        if (ctx.customRendering) {
            IGraphics->FreeBitMap(cr.bitmap);
        } else {
            RemoveLabelNodes();

            for (int i = 0; i < MAX_NODES; i++) {
                IListBrowser->FreeListBrowserNode(nodes[i]);
            }

            IListBrowser->FreeLBColumnInfo(columnInfo);

            IExec->FreeSysObject(ASOT_LIST, labelList);
        }
    }

    ctx.running = FALSE;

    CloseClasses();
}

