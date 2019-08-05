#ifndef MLPLAYOUT_H_INCLUDED
#define MLPLAYOUT_H_INCLUDED
#include <stdint.h>


struct MLP_LAYOUT
{
 uint32_t pkt_pos;
 uint32_t nb_samples;
 uint32_t rank;
};

#ifndef MAX_AOB_SECTORS
#  define MAX_AOB_SECTORS 1024 * 512 // 1024 * 1024 * 1024 (max AOB size is 1GB) / 2048 (sector size)
#endif

extern struct MLP_LAYOUT mlp_layout[MAX_AOB_SECTORS];
struct MLP_LAYOUT* get_mlp_layout(void);

#endif // MLPLAYOUT_H_INCLUDED
