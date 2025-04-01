#pragma once

#include <vector>
#include "ns3/core-module.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#define FECBLOCKSIZE 10
#define FECREDUNDANCY 1

namespace ns3 
{

class ForwardErrorCorrectionEncoder : public Object
{
public:
  /**
   * Get the type ID.
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  
  /**
   * default constructor
   */
  ForwardErrorCorrectionEncoder(void);

  /**
   * copy constructor
   */
  ForwardErrorCorrectionEncoder(const ForwardErrorCorrectionEncoder& fec);

  /**
   * Add new Packet to Fec Block.
   */
  void AddPacket(const Ptr<Packet> packet);

  /**
   * Check if Fec Block is full.
   * return True if Fec Block is full
   */
  bool FecBlockFull();

  /**
   * Return redudant packets from filled in Fec Block
   */
  std::vector<Ptr<Packet>> GetRedundantPackets();

  /**
   * Reset Fec Block
   */
  void Reset();

  /**
   * 
   */

protected:
  int m_cur_packets_in_block;
  int m_block_size;
  int m_redundancy;
};


} // namespace ns3