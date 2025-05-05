#include "ns3/fec-module-encoder.h"
#include "ns3/core-module.h"
#include "ns3/tcp-option-fec.h"
#include <iostream>

#define DEBUG

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ForwardErrorCorrectionEncoderModule");

NS_OBJECT_ENSURE_REGISTERED (ForwardErrorCorrectionEncoder);

TypeId
ForwardErrorCorrectionEncoder::GetTypeId (void)
{
    static TypeId tid = TypeId("ns3::ForwardErrorCorrectionEncoder")
        .SetParent<Object>()
        .SetGroupName("ForwardErrorCorrectionEncoder")
        .AddConstructor<ForwardErrorCorrectionEncoder>()
        .AddAttribute ("Redundancy", "Amount of redundant packets in block",
                       IntegerValue (0),
                       MakeIntegerAccessor (&ForwardErrorCorrectionEncoder::m_redundancy),
                       MakeIntegerChecker<int> ())
        .AddAttribute ("FecBlockSize", "Amount of pacets in one fec block",
                       IntegerValue(10),
                       MakeIntegerAccessor (&ForwardErrorCorrectionEncoder::m_blockSize),
                       MakeIntegerChecker<int> ());
    return tid;
}

ForwardErrorCorrectionEncoder::ForwardErrorCorrectionEncoder (int redundancy)
 :  m_curPacketsInBlock (0),
    m_blockSize (FEC_BLOCK_SIZE),
    m_redundancy (redundancy),
    m_prevPayloadSequenceNumber (SequenceNumber32 (0)),
    m_curFecSequenceNumber (SequenceNumber32 (90))
{
  NS_LOG_FUNCTION (this);
};

ForwardErrorCorrectionEncoder::ForwardErrorCorrectionEncoder (const ForwardErrorCorrectionEncoder &other) 
  : m_curPacketsInBlock (other.m_curPacketsInBlock),
    m_blockSize (other.m_blockSize),
    m_redundancy (other.m_redundancy),
    m_prevPayloadSequenceNumber (other.m_prevPayloadSequenceNumber),
    m_curFecSequenceNumber (other.m_curFecSequenceNumber)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor"); 
};

void ForwardErrorCorrectionEncoder::AddPacket (const Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  if ((int)m_fecBlock.size () != m_blockSize)
  {
    m_fecBlock.resize (m_blockSize);
  }
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionEncoder Add Packet, Previous " << m_prevPayloadSequenceNumber.GetValue () << " Packet size " << packet->GetSize () << " New Seq " << tcpHeader.GetSequenceNumber ().GetValue () << std::endl;
  #endif
  // add first packet in all or usual case
  if (m_prevPayloadSequenceNumber.GetValue () == 0 || m_prevPayloadSequenceNumber.GetValue () + packet->GetSize () - 32 == tcpHeader.GetSequenceNumber ().GetValue ())
  {
    m_fecBlock[m_curPacketsInBlock] = std::make_pair (packet, tcpHeader);
    m_prevPayloadSequenceNumber = SequenceNumber32 (tcpHeader.GetSequenceNumber ().GetValue ());;
    m_curPacketsInBlock += 1;
    #ifdef DEBUG
    std::cout << "ForwardErrorCorrectionEncoder Add Packet success" << std::endl;
    #endif
  }
  else
  {
    #ifdef DEBUG
    std::cout << "ForwardErrorCorrectionEncoder Add Packet not success" << std::endl;
    #endif
    return;
  }
}

bool ForwardErrorCorrectionEncoder::FecBlockFull ()
{
  #ifdef DEBUG
  std::cout << "Encoder FecBlockFull " << ((m_blockSize <= m_curPacketsInBlock + m_redundancy) ? "true" : "false") << std::endl;
  #endif
  return m_blockSize <= m_curPacketsInBlock + m_redundancy;
}

std::vector<std::pair<Ptr<Packet>, TcpHeader>> ForwardErrorCorrectionEncoder::GetRedundantPackets ()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionEncoder GetRedundantPackets, current Fec Seq " << m_curFecSequenceNumber.GetValue () << std::endl;
  #endif

  std::vector<std::pair<Ptr<Packet>, TcpHeader>> result;
  
  // dummy packet to send
  Ptr<Packet> redundantPacket = Create<Packet> ((m_fecBlock[0].first->GetSize ()));
  TcpHeader redundantHeader = TcpHeader ();
  SetFecHeader (redundantHeader);
  for (int i = 0; i < m_redundancy; ++i)
  {
    redundantHeader.SetSequenceNumber (m_curFecSequenceNumber);
    m_curFecSequenceNumber = SequenceNumber32 (m_curFecSequenceNumber.GetValue () + redundantPacket->GetSize ());
    #ifdef DEBUG
    std::cout << "ForwardErrorCorrectionEncoder GetRedundantPackets, packet seq to return  " << redundantHeader.GetSequenceNumber ().GetValue () << " fec packet size " << redundantPacket->GetSize () << std::endl;
    #endif
    result.push_back (std::make_pair (redundantPacket, redundantHeader));
  }
  return result;
}

void ForwardErrorCorrectionEncoder::Reset ()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionEncoder Reset" << std::endl;
  #endif
  m_curPacketsInBlock = 0;
}

void ForwardErrorCorrectionEncoder::SetFecHeader (TcpHeader &header)
{
  Ptr<TcpOptionFec> option = CreateObject<TcpOptionFec> ();
  header.AppendOption (option);
}

bool ForwardErrorCorrectionEncoder::IsOn ()
{
  return m_redundancy > 0;
}

SequenceNumber32 ForwardErrorCorrectionEncoder::GetNextSeqNum () const
{
  return SequenceNumber32 (m_curFecSequenceNumber.GetValue () + m_fecBlock[0].first->GetSize ());
}

} // namespace ns3