#ifndef C63_SISCI_VARIABLES_H_
#define C63_SISCI_VARIABLES_H_

#include <inttypes.h>
#include <stdint.h>

#define NO_FLAGS 0
#define NO_CALLBACK NULL

#define GROUP 6

#ifndef GROUP
#error Fill in group number in common.h!
#endif

/* GET_SEGMENTIUD(2) gives you segmentid 2 at your groups offset */
#define GET_SEGMENTID(id) ( GROUP << 16 | id )

// segment for image transfer
#define SEGMENT_CLIENT GET_SEGMENTID(1)
#define SEGMENT_SERVER GET_SEGMENTID(2)

// segment for pio communication
#define SEGMENT_CLIENT_COMMS GET_SEGMENTID(3)
#define SEGMENT_SERVER_COMMS GET_SEGMENTID(4)

// segment for encoding results
#define SEGMENT_CLIENT_RESULT GET_SEGMENTID(5)
#define SEGMENT_SERVER_RESULT GET_SEGMENTID(6)


/*
*   cmd what the client and server uses to communicate to eachother
*     - CMD_INVALID is used as a signal to the other to Wait
*     - CMD_DONE    is used as a signal to stop waiting
*     - CMD_QUIT    is used as a signal for the server/tegra to exit
*/
enum cmd
{
    CMD_INVALID,
    CMD_QUIT,
    CMD_DONE,
};


// holds CMD command
struct packet
{
  union {
    struct{
      uint8_t cmd;
      int width;
      int height;
    };
  };
};

//  used as communication segments between client and server, contains a packet.
struct comms {
  struct packet packet;
};

#endif  /* C63_SISCI_VARIABLES_H_ */
