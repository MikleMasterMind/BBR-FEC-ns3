#pragma once

#include <vector>
#include "ns3/core-module.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/tcp-header.h"

#define FECBLOCKSIZE 10
#define FECREDUNDANCY 1
#define FECLOSSTHRESH 1

namespace ns3 
{

class ForwardErrorCorrectionDecoder : public Object
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
  ForwardErrorCorrectionDecoder(void);

  /**
   * copy constructor
   */
  ForwardErrorCorrectionDecoder(const ForwardErrorCorrectionDecoder& fec);

  /**
   * Add new Packet to Fec Block.
   */
  void AddPacket(const Ptr<Packet> packet, const TcpHeader& header);

  /**
   * Notify Fec about packet loss
   */
  void AddLoss(int loss_count);

  /**
   * Check if Fec Block is full.
   * return True if Fec Block is full
   */
  bool FecBlockFull();

  /**
   * Check if recovery packets loss possible
   * return True if possible
   */
  bool RecoveryPossible();

  /**
   * Return payload packets from filled in Fec Block
   */
  std::vector<Ptr<Packet>> GetPayloadPackets();

  /**
   * Reset Fec Block
   */
  void Reset();

protected:
  int m_cur_packets_in_block;
  int m_block_size;
  int m_redundancy;
  int m_loss;
  int m_loss_thresh;
};

} // namespace ns3