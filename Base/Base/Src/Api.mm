#include "StdAfx.h"
#include "Api.h"
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGLES/EAGLDrawable.h>

namespace xs {
    
bool GetClientRect(void* hwnd,Rect* rc)
{
    /*
    UIView* view = (UIView*)hwnd;
    
    CAEAGLLayer *eaglLayer = (CAEAGLLayer *)(view->_layer);
    
    CGSize					newSize;
    newSize = [eaglLayer bounds].size;
    
    rc->left = 0;
    rc->top = 0;
    rc->right = newSize.width;
    rc->bottom = newSize.height;
    */
    //ios do nothing!
    return true;
}
    
const char* getAppPath()
{
    NSString* path = [[NSBundle mainBundle] bundlePath];
    static std::string str = [path cStringUsingEncoding:NSASCIIStringEncoding];
    return str.c_str();
}

}