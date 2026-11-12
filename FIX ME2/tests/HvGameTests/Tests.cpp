#include <stdio.h>
#include <math.h>
#include "ourcute/geometry.h"
int main() {
    int assertions=0,failed=0;
    ourcute::Matrix4 m={};m.m[0]=1;m.m[5]=1;m.m[15]=1;
    ourcute::Vec2 p={};
    bool ok=ourcute::world_to_screen({0,0,0},&m,800,600,&p);
    ++assertions;if(!ok||p.x!=400||p.y!=300)++failed;
    m.m[15]=-1;
    ++assertions;if(ourcute::world_to_screen({0,0,0},&m,800,600,&p))++failed;
    m={};m.m[0]=1;m.m[6]=1;m.m[15]=1;
    ourcute::Box box={};
    ++assertions;if(!ourcute::make_box({0,0,-0.5f},{0,0,0.5f},&m,800,600,&box))++failed;
    ++assertions;if(fabsf(box.left-336.1702f)>0.01f||fabsf(box.right-463.8298f)>0.01f)++failed;
    ++assertions;if(box.top!=150||box.bottom!=450)++failed;
    ++assertions;if(ourcute::make_box({0,0,0.5f},{0,0,-0.5f},&m,800,600,&box))++failed;
    ++assertions;if(ourcute::make_box({0,0,0},{0,0,1},nullptr,800,600,&box))++failed;
    ++assertions;if(ourcute::world_to_screen({0,0,0},&m,0,600,&p))++failed;
    printf("HvGameTests geometry: %d assertions, %d failures\n",assertions,failed);
    return failed?1:0;
}
