#include "ns3/fec-module-decoder.h"
#include "ns3/core-module.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ForwardErrorCorrectionDecoderModule");

// Регистрация модуля
NS_OBJECT_ENSURE_REGISTERED(ForwardErrorCorrectionDecoder);

TypeId
ForwardErrorCorrectionDecoder::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::ForwardErrorCorrectionDecoder")
        .SetParent<Object>()
        .SetGroupName("ForwardErrorCorrectionDecoder")
        .AddConstructor<ForwardErrorCorrectionDecoder>();
    return tid;
}

ForwardErrorCorrectionDecoder::ForwardErrorCorrectionDecoder(void)
{
  m_block_size = FECBLOCKSIZE;
  m_redundancy = FECREDUNDANCY;
  m_cur_packets_in_block = 0;
  m_loss = 0;
  m_loss_thresh = FECLOSSTHRESH;

  NS_LOG_FUNCTION (this);
};

ForwardErrorCorrectionDecoder::ForwardErrorCorrectionDecoder(const ForwardErrorCorrectionDecoder &other) 
  : m_cur_packets_in_block (other.m_cur_packets_in_block),
    m_block_size (other.m_block_size),
    m_redundancy (other.m_redundancy),
    m_loss (other.m_loss),
    m_loss_thresh (other.m_loss_thresh)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor"); 
};

void ForwardErrorCorrectionDecoder::AddPacket(const Ptr<Packet> packet, const TcpHeader& header)
{
  m_cur_packets_in_block += 1;
}

void ForwardErrorCorrectionDecoder::AddLoss(int loss_count)
{
  m_loss += loss_count;
}

bool ForwardErrorCorrectionDecoder::FecBlockFull()
{
  return m_block_size == m_cur_packets_in_block + m_redundancy;
}

bool ForwardErrorCorrectionDecoder::RecoveryPossible()
{
  return m_loss <= m_loss_thresh;
}

std::vector<Ptr<Packet>> 
ForwardErrorCorrectionDecoder::GetPayloadPackets()
{
  std::vector<Ptr<Packet>> result;
  for (int i = 0; i < m_loss; ++i)
  {
    Ptr<Packet> redundant = Create<Packet>(0);
    result.push_back(redundant);
  }
  return result;
}

void ForwardErrorCorrectionDecoder::Reset()
{
  m_cur_packets_in_block = 0;
  m_loss = 0;
}

} // namespace ns3