#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
static Display *d;static Window root,w;
static void check(int width,int height) {
 usleep(600000); XWindowAttributes a,b;
 XGetWindowAttributes(d,root,&a);XGetWindowAttributes(d,w,&b);
 printf("requested=%dx%d root=%dx%d client=%dx%d\n",width,height,a.width,a.height,b.width,b.height);
 if(a.width!=width||a.height!=height||b.width!=width||b.height!=height)exit(1);
}
static unsigned long pixel(void) {
 XImage *image=XGetImage(d,root,100,100,1,1,AllPlanes,ZPixmap);
 if(!image)exit(5);
 unsigned long value=XGetPixel(image,0,0);XDestroyImage(image);return value;
}
static void expect_action(unsigned long expected) {
 Atom type;int format;unsigned long n,left;unsigned char *data=NULL;
 XGetWindowProperty(d,root,XInternAtom(d,"_RUNELITE_HOST_ACTION",False),0,3,False,XA_CARDINAL,&type,&format,&n,&left,&data);
 unsigned long actual=(data&&n==3)?((unsigned long*)data)[0]:0;
 printf("host action expected=%lu actual=%lu\n",expected,actual);fflush(stdout);
 if(data)XFree(data);
 if(actual!=expected)exit(4);
}
static void state(int action,int expected) {
 unsigned long v[3]={action,0,0};
 XChangeProperty(d,root,XInternAtom(d,"_RUNELITE_HOST_ACTION",False),XA_CARDINAL,32,PropModeReplace,(unsigned char*)v,3);XFlush(d);usleep(900000);
 Atom type;int format;unsigned long n,left;unsigned char *data=NULL;
 XGetWindowProperty(d,w,XInternAtom(d,"_NET_WM_STATE",False),0,4,False,XA_ATOM,&type,&format,&n,&left,&data);
 printf("host action=%d Java state atoms=%lu\n",action,n);
 if((n==2)!=expected)exit(2);
 if(data)XFree(data);
}
int main(void) {
 d=XOpenDisplay(NULL);if(!d)return 3;root=DefaultRootWindow(d);
 w=XCreateSimpleWindow(d,root,0,0,2000,1200,0,0,0);
 XSetWindowAttributes attrs={.bit_gravity=NorthWestGravity};
 XChangeWindowAttributes(d,w,CWBitGravity,&attrs);
 XMapWindow(d,w);XFlush(d);usleep(600000);
 GC gc=XCreateGC(d,w,0,NULL);XSetForeground(d,gc,0x123456);
 XFillRectangle(d,w,gc,0,0,2000,1200);XSync(d,False);
 unsigned long before=pixel();
 /* A sidebar can resize/reposition while its click is still held. */
 XTestFakeMotionEvent(d,-1,1000,500,0);XTestFakeButtonEvent(d,1,True,0);XSync(d,False);
 XMoveResizeWindow(d,w,-200,0,2400,1200);XFlush(d);check(2400,1200);
 expect_action(8);
 unsigned long after=pixel();
 printf("retained pixel before=%lx after=%lx\n",before,after);fflush(stdout);
 if(after!=before)exit(6);
 /* Position-only title-bar movement must still reach the host. */
 XMoveWindow(d,w,10,0);XFlush(d);usleep(300000);expect_action(1);
 XTestFakeButtonEvent(d,1,False,0);XFlush(d);
 XResizeWindow(d,w,2000,1200);XFlush(d);check(2000,1200);
 if(pixel()!=before)exit(7);
 state(3,1);state(4,0);
 XDestroyWindow(d,w);XCloseDisplay(d);return 0;
}
