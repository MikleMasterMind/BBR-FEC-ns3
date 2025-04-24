#include "ns3/tcp-option-fec.h"
#include "ns3/core-module.h"


namespace ns3
{

const int OPTION_SIZE = 2;

TypeId TcpOptionFec::GetTypeId ()
{
  static TypeId tid = TypeId ("ns3::TcpOptionFec")
        .SetParent<TcpOption> ()
        .AddConstructor<TcpOptionFec> ();
  return tid;
}

TcpOptionFec::TcpOptionFec ()
{

}

TcpOptionFec::TcpOptionFec (const TcpOptionFec &option)
{

}

void TcpOptionFec::Serialize (Buffer::Iterator iter) const
{
  iter.WriteU8 (GetKind ());
  iter.WriteU8 (OPTION_SIZE);
}

uint32_t ns3::TcpOptionFec::Deserialize (ns3::Buffer::Iterator iter)
{
  uint8_t kind = iter.ReadU8 ();
  NS_ASSERT (kind == GetKind ());
  uint8_t len = iter.ReadU8 ();
  NS_ASSERT (len == OPTION_SIZE);
  return GetSerializedSize ();
}

uint8_t TcpOptionFec::GetKind () const
{
  return 253;
}

uint32_t TcpOptionFec::GetSerializedSize () const
{
    return OPTION_SIZE; // 1 (kind) + 1 (length) + 0 (data)
}

void ns3::TcpOptionFec::Print(std::ostream &os) const
{

}

}