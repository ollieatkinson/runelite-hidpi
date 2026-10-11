/* Private single-client WM. Forward RuneLite controls to the rootful host. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Display *d;
static Window root, mainwin, check;
static int rw, rh, maximized;
static Atom host, netstate, maxh, maxv, moveresize, wmchange;
static void action(unsigned long id, unsigned long a, unsigned long b) {
    unsigned long v[3]={id,a,b};
    XChangeProperty(d,root,host,XA_CARDINAL,32,PropModeReplace,(unsigned char *)v,3);
}
static void wm_state(Window window,long state) {
    unsigned long values[2]={(unsigned long)state,None};
    Atom atom=XInternAtom(d,"WM_STATE",False);
    XChangeProperty(d,window,atom,atom,32,PropModeReplace,(unsigned char *)values,2);
}
static void fill(void) {
    if (!mainwin) return;
    XMoveResizeWindow(d,mainwin,0,0,rw,rh);
    XEvent e;memset(&e,0,sizeof(e));
    e.xconfigure.type=ConfigureNotify;
    e.xconfigure.display=d;
    e.xconfigure.event=e.xconfigure.window=mainwin;
    e.xconfigure.width=rw; e.xconfigure.height=rh;
    e.xconfigure.send_event=True;
    e.xconfigure.x=0;e.xconfigure.y=0;e.xconfigure.border_width=0;e.xconfigure.above=None;e.xconfigure.override_redirect=False;
    XSendEvent(d,mainwin,False,StructureNotifyMask,&e);
}
static void title(void) {
    char *name=NULL;
    XFetchName(d,mainwin,&name);
    if (name) {
        XChangeProperty(d,root,XInternAtom(d,"_RUNELITE_HOST_TITLE",False),
            XInternAtom(d,"UTF8_STRING",False),8,PropModeReplace,(unsigned char *)name,strlen(name));
        XFree(name);
    }
}
static void sync_host_state(void) {
    Atom type; int format; unsigned long n,left; unsigned char *data=NULL;
    XGetWindowProperty(d,root,XInternAtom(d,"_RUNELITE_HOST_STATE",False),0,1,False,
        XA_CARDINAL,&type,&format,&n,&left,&data);
    if(data && format==32 && n==1) {
        maximized=(*(unsigned long *)data)&1;
        if(mainwin) {
            Atom state[]={maxh,maxv};
            XChangeProperty(d,mainwin,netstate,XA_ATOM,32,PropModeReplace,
                (unsigned char *)state,maximized?2:0);
        }
        printf("STATE %d\n",maximized);
    }
    if(data) XFree(data);
}
static void minsize(void) {
    XSizeHints h;long supplied;
    if(XGetWMNormalHints(d,mainwin,&h,&supplied) && (h.flags&PMinSize))
        action(9,h.min_width,h.min_height);
}
static void chrome(void) {
    Atom type; int format; unsigned long n,left; unsigned char *data=NULL;
    XGetWindowProperty(d,mainwin,XInternAtom(d,"_MOTIF_WM_HINTS",False),0,5,False,
        AnyPropertyType,&type,&format,&n,&left,&data);
    if (data && format==32 && n>=3) {
        unsigned long *v=(unsigned long *)data;
        if ((v[0]&2) && v[2]==0) action(6,0,0);
    }
    if(data) XFree(data);
}
int main(void) {
    setvbuf(stdout,NULL,_IOLBF,0);
    d=XOpenDisplay(NULL); if(!d) return 1;
    root=DefaultRootWindow(d);
    XWindowAttributes a; XGetWindowAttributes(d,root,&a);rw=a.width;rh=a.height;
    host=XInternAtom(d,"_RUNELITE_HOST_ACTION",False);
    netstate=XInternAtom(d,"_NET_WM_STATE",False);
    maxh=XInternAtom(d,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
    maxv=XInternAtom(d,"_NET_WM_STATE_MAXIMIZED_VERT",False);
    moveresize=XInternAtom(d,"_NET_WM_MOVERESIZE",False);
    wmchange=XInternAtom(d,"WM_CHANGE_STATE",False);
    Atom supported[]={netstate,maxh,maxv,moveresize,XInternAtom(d,"_NET_SUPPORTING_WM_CHECK",False)};
    XChangeProperty(d,root,XInternAtom(d,"_NET_SUPPORTED",False),XA_ATOM,32,PropModeReplace,(unsigned char *)supported,5);
    check=XCreateSimpleWindow(d,root,-1,-1,1,1,0,0,0);
    Atom wmcheck=XInternAtom(d,"_NET_SUPPORTING_WM_CHECK",False);
    XChangeProperty(d,root,wmcheck,XA_WINDOW,32,PropModeReplace,(unsigned char *)&check,1);
    XChangeProperty(d,check,wmcheck,XA_WINDOW,32,PropModeReplace,(unsigned char *)&check,1);
    const char *name="LG3D";
    XChangeProperty(d,check,XInternAtom(d,"_NET_WM_NAME",False),XInternAtom(d,"UTF8_STRING",False),8,PropModeReplace,(const unsigned char *)name,strlen(name));
    XSelectInput(d,root,SubstructureRedirectMask|SubstructureNotifyMask|StructureNotifyMask|PropertyChangeMask);
    XSync(d,False);
    for (;;) {
        XEvent e;XNextEvent(d,&e);
        if(e.type==MapRequest) {
            Window w=e.xmaprequest.window,t=0;XGetTransientForHint(d,w,&t);
            wm_state(w,NormalState);
            if(!mainwin&&!t) {
                mainwin=w;maximized=0;
                unsigned long extents[4]={0,0,0,0};
                XChangeProperty(d,w,XInternAtom(d,"_NET_FRAME_EXTENTS",False),XA_CARDINAL,32,PropModeReplace,(unsigned char *)extents,4);
                XSelectInput(d,w,PropertyChangeMask);
                sync_host_state();fill();title();chrome();minsize();
                puts("CLIENT shown");
                XChangeProperty(d,root,XInternAtom(d,"_NET_ACTIVE_WINDOW",False),XA_WINDOW,32,PropModeReplace,(unsigned char *)&w,1);
            }
            XMapWindow(d,w);XSetInputFocus(d,w,RevertToParent,CurrentTime);
        } else if(e.type==ConfigureRequest) {
            XConfigureRequestEvent *c=&e.xconfigurerequest;
            if(c->window==mainwin) {
                int resizing=((c->value_mask&CWWidth) && c->width!=rw)
                    || ((c->value_mask&CWHeight) && c->height!=rh);
                if(resizing) {
                    action(8,(c->value_mask&CWWidth)?c->width:rw,(c->value_mask&CWHeight)?c->height:rh);
                    XSync(d,False);
                    XGetWindowAttributes(d,root,&a);rw=a.width;rh=a.height;
                }
                /* Automatic sidebar resize can include containment coordinates.
                 * It is not a user title-bar drag, even if its click is held. */
                if(!resizing && (c->value_mask&(CWX|CWY)) && (c->x||c->y)) {
                    Window rr,child;int x,y,wx,wy;unsigned mask;
                    XQueryPointer(d,root,&rr,&child,&x,&y,&wx,&wy,&mask);
                    if(mask&Button1Mask) action(1,0,0);
                }
                fill();
            } else {
                XWindowChanges v={.x=c->x,.y=c->y,.width=c->width,.height=c->height,
                    .border_width=c->border_width,.sibling=c->above,.stack_mode=c->detail};
                XConfigureWindow(d,c->window,c->value_mask,&v);
            }
        } else if(e.type==ClientMessage && e.xclient.window==mainwin) {
            XClientMessageEvent *c=&e.xclient;
            if(c->message_type==moveresize) {
                long direction=c->data.l[2];
                static const unsigned edges[]={5,1,9,8,10,2,6,4};
                if(direction==8) action(1,0,0);
                else if(direction>=0&&direction<8) action(2,edges[direction],0);
            } else if(c->message_type==netstate &&
                ((Atom)c->data.l[1]==maxh||(Atom)c->data.l[1]==maxv||(Atom)c->data.l[2]==maxh||(Atom)c->data.l[2]==maxv)) {
                maximized=c->data.l[0]==2?!maximized:c->data.l[0]==1;
                printf("STATE %d\n",maximized);
                Atom state[]={maxh,maxv};
                XChangeProperty(d,mainwin,netstate,XA_ATOM,32,PropModeReplace,(unsigned char *)state,maximized?2:0);
                action(maximized?3:4,0,0);
            } else if(c->message_type==wmchange && c->data.l[0]==IconicState) action(5,0,0);
        } else if(e.type==ConfigureNotify && e.xconfigure.window==root) {
            XGetWindowAttributes(d,root,&a);rw=a.width;rh=a.height;
            fprintf(stderr,"ROOT %dx%d\n",rw,rh);fill();
        } else if(e.type==PropertyNotify && e.xproperty.window==root &&
                  e.xproperty.atom==XInternAtom(d,"_RUNELITE_HOST_STATE",False)) {
            sync_host_state();
        } else if(e.type==PropertyNotify && e.xproperty.window==mainwin) {
            if(e.xproperty.atom==XA_WM_NAME||e.xproperty.atom==XInternAtom(d,"_NET_WM_NAME",False))title();
            if(e.xproperty.atom==XInternAtom(d,"_MOTIF_WM_HINTS",False))chrome();
            if(e.xproperty.atom==XA_WM_NORMAL_HINTS)minsize();
        } else if(e.type==DestroyNotify && e.xdestroywindow.window==mainwin) {mainwin=0;puts("CLIENT closed");}
        XFlush(d);
    }
}
