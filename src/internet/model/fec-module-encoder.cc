#include "ns3/fec-module-encoder.h"
#include "ns3/core-module.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("ForwardErrorCorrectionEncoderModule");

// Регистрация модуля
NS_OBJECT_ENSURE_REGISTERED(ForwardErrorCorrectionEncoder);

TypeId
ForwardErrorCorrectionEncoder::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::ForwardErrorCorrectionEncoder")
        .SetParent<Object>()
        .SetGroupName("ForwardErrorCorrectionEncoder")
        .AddConstructor<ForwardErrorCorrectionEncoder>();
    return tid;
}

ForwardErrorCorrectionEncoder::ForwardErrorCorrectionEncoder(void)
{
  m_block_size = FECBLOCKSIZE;
  m_redundancy = FECREDUNDANCY;
  m_cur_packets_in_block = 0;

  NS_LOG_FUNCTION (this);
};

ForwardErrorCorrectionEncoder::ForwardErrorCorrectionEncoder(const ForwardErrorCorrectionEncoder &other) 
  : m_cur_packets_in_block (other.m_cur_packets_in_block),
    m_block_size (other.m_block_size),
    m_redundancy (other.m_redundancy)
    
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor"); 
};

void ForwardErrorCorrectionEncoder::AddPacket(const Ptr<Packet> packet)
{
  m_cur_packets_in_block += 1;
}

bool ForwardErrorCorrectionEncoder::FecBlockFull()
{
  return m_block_size == m_cur_packets_in_block + m_redundancy;
}

std::vector<Ptr<Packet>> ForwardErrorCorrectionEncoder::GetRedundantPackets()
{
  std::vector<Ptr<Packet>> result;
  for (int i = 0; i < m_redundancy; ++i)
  {
    Ptr<Packet> redundant = Create<Packet>(0);
    result.push_back(redundant);
  }
  return result;
}

void ForwardErrorCorrectionEncoder::Reset()
{
  m_cur_packets_in_block = 0;
}

} // namespace ns3