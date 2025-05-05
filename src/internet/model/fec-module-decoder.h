#pragma once

#include <set>
#include <array>
#include "ns3/core-module.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/tcp-header.h"

#define FEC_BLOCK_SIZE 10
#define FEC_REDUNDANCY 1
#define FEC_LOSS_THRESH 1
#define FEC_RAND_FEC_SEQ_NUM 0

#define FEC_RAND_SEQ_NUM 7

#define FEC_TCP_HEADER_FLAG (ns3::TcpHeader::SYN | ns3::TcpHeader::FIN)

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
  ForwardErrorCorrectionDecoder (void);

  /**
   * copy constructor
   */
  ForwardErrorCorrectionDecoder (const ForwardErrorCorrectionDecoder& fec);

  /**
   * Add new Packet to Fec Block.
   */
  void AddPacket(const Ptr<Packet> packet, const TcpHeader& header);

  /**
   * Check if Fec Block is full.
   * return True if Fec Block is full
   */
  bool FecBlockFull() const;

  /**
   * Check if recovery packets loss possible
   * return True if possible
   */
  bool RecoveryPossible() const;

  /**
   * Return payload packets from filled in Fec Block
   * All packets, not only recoverd
   */
  std::vector<std::pair<Ptr<Packet>, TcpHeader>> GetRecoveredPackets();

  /**
   * Reset Fec Block
   */
  void Reset();

  /**
   * Return true if header has TcpOptionFec
   * false othervise
   */
  bool IsFecHeader (const TcpHeader& header);

  /**
   * Retun next expected sequence number of fec packet
   */
  SequenceNumber32 GetExpectedSeqNum () const;
  
protected:
  int m_curPacketsInBlock;
  int m_blockSize;
  int m_redundancy;
  int m_lossThresh;
  std::set<int> m_lossIndexes;
  std::vector<std::pair<Ptr<Packet>, TcpHeader>> m_fecBlock;
  SequenceNumber32 m_expectedFecSeqeunceNumber;
  SequenceNumber32 m_expectedPayloadSequnceNumber;
};

} // namespace ns3