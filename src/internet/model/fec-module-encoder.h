#pragma once

#include <vector>
#include "ns3/core-module.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/tcp-header.h"
#include "ns3/sequence-number.h"
#include "ns3/tcp-option.h"

#define FEC_BLOCK_SIZE 10
#define FEC_TCP_HEADER_FLAG (ns3::TcpHeader::SYN | ns3::TcpHeader::FIN)

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
  ForwardErrorCorrectionEncoder (int redundancy = 0);

  /**
   * copy constructor
   */
  ForwardErrorCorrectionEncoder (const ForwardErrorCorrectionEncoder& fec);

  /**
   * Add new Packet to Fec Block.
   */
  void AddPacket (const Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * Check if Fec Block is full.
   * return True if Fec Block is full
   */
  bool FecBlockFull ();

  /**
   * Return redudant packets from filled in Fec Block
   */
  std::vector<std::pair<Ptr<Packet>, TcpHeader>> GetRedundantPackets ();

  /**
   * Reset Fec Block
   */
  void Reset();

  /**
   * Return true if generating fec packets is on
   * false oservise
   */
  bool IsOn ();

  /**
   * Add TcpOptionFec in header
   */
  void SetFecHeader (TcpHeader& header);

  /**
   * Return seq number which should be send next
   */
  SequenceNumber32 GetNextSeqNum () const;

protected:
  int m_curPacketsInBlock;
  int m_blockSize;
  int m_redundancy;
  std::vector<std::pair<Ptr<Packet>, TcpHeader>> m_fecBlock;
  SequenceNumber32 m_prevPayloadSequenceNumber;
  SequenceNumber32 m_curFecSequenceNumber;
};


} // namespace ns3