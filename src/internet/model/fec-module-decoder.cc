#include "ns3/fec-module-decoder.h"
#include "ns3/core-module.h"

#include <iostream>
// #define DEBUG

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("ForwardErrorCorrectionDecoderModule");

NS_OBJECT_ENSURE_REGISTERED (ForwardErrorCorrectionDecoder);

TypeId
ForwardErrorCorrectionDecoder::GetTypeId (void)
{
    static TypeId tid = TypeId("ns3::ForwardErrorCorrectionDecoder")
        .SetParent<Object>()
        .SetGroupName("ForwardErrorCorrectionDecoder")
        .AddConstructor<ForwardErrorCorrectionDecoder> ();
    return tid;
}

ForwardErrorCorrectionDecoder::ForwardErrorCorrectionDecoder (void)
  : m_curPacketsInBlock (0),
    m_blockSize (FEC_BLOCK_SIZE),
    m_redundancy (4),
    m_lossThresh (FEC_LOSS_THRESH),
    m_expectedFecSeqeunceNumber (0),
    m_expectedPayloadSequnceNumber (FEC_RAND_SEQ_NUM)
{
  NS_LOG_FUNCTION (this);
};

ForwardErrorCorrectionDecoder::ForwardErrorCorrectionDecoder (const ForwardErrorCorrectionDecoder &other) 
  : m_curPacketsInBlock (other.m_curPacketsInBlock),
    m_blockSize (other.m_blockSize),
    m_redundancy (other.m_redundancy),
    m_lossThresh (other.m_lossThresh),
    m_lossIndexes (other.m_lossIndexes),
    m_fecBlock (other.m_fecBlock),
    m_expectedFecSeqeunceNumber (other.m_expectedFecSeqeunceNumber),
    m_expectedPayloadSequnceNumber (other.m_expectedPayloadSequnceNumber)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor"); 
};

void ForwardErrorCorrectionDecoder::AddPacket (const Ptr<Packet> packet, const TcpHeader& tcpHeader)
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionDecoder AddPacket, " << packet->GetSize () << std::endl;
  uint8_t buffer[1500]; // MTU-typical size
  packet->CopyData (buffer, 7);
  std::string receivedMessage ((char*)buffer, 6);
  std::cout << receivedMessage << std::endl;
  #endif
  if (isFecHeader (tcpHeader))
  {
    #ifdef DEBUG
    std::cout << "fec packet, seq num " << tcpHeader.GetSequenceNumber ().GetValue () << " expected " << m_expectedFecSeqeunceNumber.GetValue ();
    #endif
    if (tcpHeader.GetSequenceNumber ().GetValue () == m_expectedFecSeqeunceNumber.GetValue ())
    {
      m_redundancy += 1;
      m_expectedFecSeqeunceNumber = SequenceNumber32 (tcpHeader.GetSequenceNumber ().GetValue () + packet->GetSize ());
      #ifdef DEBUG
      std::cout << " success" << std::endl;
      #endif
    }
  }
  else
  {
    #ifdef DEBUG
    std::cout << "payload packet, seq num " << tcpHeader.GetSequenceNumber ().GetValue () << " Packet size " << packet->GetSize () << " expected " << m_expectedPayloadSequnceNumber.GetValue ();
    #endif
    // first packet in all or good case
    if ((m_expectedPayloadSequnceNumber.GetValue () == FEC_RAND_SEQ_NUM) || (m_expectedPayloadSequnceNumber.GetValue () == tcpHeader.GetSequenceNumber ().GetValue ()))
    {
      m_fecBlock[m_curPacketsInBlock] = std::make_pair (packet, tcpHeader);
      m_curPacketsInBlock += 1;
      m_expectedPayloadSequnceNumber = SequenceNumber32 (tcpHeader.GetSequenceNumber ().GetValue () + packet->GetSize ());
      #ifdef DEBUG
      std::cout << " => success" << std::endl;
      #endif
    }
    else if (m_expectedPayloadSequnceNumber.GetValue () < tcpHeader.GetSequenceNumber ().GetValue ())
    {
      #ifdef DEBUG
      std::cout << " => pacet loss ";
      #endif
      SequenceNumber32 curSequnceNumber = m_expectedPayloadSequnceNumber;
      int lossIndex = m_curPacketsInBlock;
      while (curSequnceNumber.GetValue () < tcpHeader.GetSequenceNumber ().GetValue () && lossIndex < m_blockSize)
      {
        m_lossIndexes.insert (lossIndex);
        lossIndex += 1;
        #ifdef DEBUG
        std::cout << " loss index " << lossIndex - 1;
        #endif
      }
      if (curSequnceNumber.GetValue () == tcpHeader.GetSequenceNumber ().GetValue ())
      {
        m_fecBlock[m_curPacketsInBlock] = std::make_pair (packet, tcpHeader);
        m_curPacketsInBlock += 1;
        m_expectedPayloadSequnceNumber = SequenceNumber32 (tcpHeader.GetSequenceNumber ().GetValue () + packet->GetSize ());
      }
      #ifdef DEBUG
      std::cout << std::endl;
      #endif
    }
  }
}

bool ForwardErrorCorrectionDecoder::FecBlockFull ()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionDecoder FecBlockFull " << (m_blockSize <= m_curPacketsInBlock + m_redundancy + (int)m_lossIndexes.size () ? "true" : "false") << std::endl;
  #endif
  return m_blockSize <= m_curPacketsInBlock + m_redundancy + (int)m_lossIndexes.size ();
}

bool ForwardErrorCorrectionDecoder::RecoveryPossible ()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionDecoder RecoveryPossible " << ((int)m_lossIndexes.size () <= m_lossThresh ? "true" : "false") << std::endl;
  #endif
  return (int)m_lossIndexes.size () <= m_lossThresh;
}

std::vector<std::pair<Ptr<Packet>, TcpHeader>> 
ForwardErrorCorrectionDecoder::GetRecoveredPackets ()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionDecoder GetRecoveredPackets ";
  #endif
  if (!RecoveryPossible ())
  {
    return std::vector<std::pair<Ptr<Packet>, TcpHeader>> ();
  }

  Ptr<Packet> fakePacket;
  TcpHeader fakeHeader;
  for (int i = 0; i < m_blockSize; ++i)
  {
    if (m_lossIndexes.count (i) == 0)
    {
      fakePacket = m_fecBlock[i].first;
      fakeHeader = m_fecBlock[i].second;
      fakeHeader.SetSequenceNumber (SequenceNumber32 (fakeHeader.GetSequenceNumber ().GetValue () - (fakePacket->GetSize () * i)));
      break;
    }
  }

  std::vector<std::pair<Ptr<Packet>, TcpHeader>> result;
  for (int i = 0; i < (m_blockSize - m_redundancy); ++i)
  {
    if (m_lossIndexes.count (i) > 0)
    {
      result.push_back(std::make_pair(fakePacket, fakeHeader));
      fakeHeader.SetSequenceNumber (SequenceNumber32(fakeHeader.GetSequenceNumber ().GetValue () + fakePacket->GetSize ()));
    }
    else
    {
      result.push_back(m_fecBlock[i]);
      fakePacket = m_fecBlock[i].first;
      fakeHeader = m_fecBlock[i].second;
    }
  }
  #ifdef DEBUG
  std::cout << "return packets " << result.size () << std::endl;
  #endif
  return result;
}

void ForwardErrorCorrectionDecoder::Reset()
{
  #ifdef DEBUG
  std::cout << "ForwardErrorCorrectionDecoder Reset" << std::endl;
  #endif
  m_curPacketsInBlock = 0;
  m_redundancy = 0;
  m_lossIndexes.clear ();
}

bool ForwardErrorCorrectionDecoder::isFecHeader(const TcpHeader &header)
{
  #ifdef DEBUG
  if (header.HasOption (TcpOption::FEC))
  {
    std::cout << "has option" << std::endl;
  }
  else
  {
    std::cout << "no option" << std::endl;
  }
  #endif
  return header.HasOption (TcpOption::FEC);
}

} // namespace ns3