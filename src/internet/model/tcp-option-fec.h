#pragma once

#include "ns3/tcp-option.h"
#include "ns3/core-module.h"

namespace ns3
{


class TcpOptionFec : public TcpOption
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
  TcpOptionFec (void);

  /**
   * copy constructor
   */
  TcpOptionFec (const TcpOptionFec& option);

  /**
   * \brief Serialize the Option to a buffer iterator
   * \param start the buffer iterator
   */
  virtual void Serialize (Buffer::Iterator iter) const;

  /**
   * \brief Deserialize the Option from a buffer iterator
   * \param start the buffer iterator
   * \returns the number of deserialized bytes
   */
  virtual uint32_t Deserialize (Buffer::Iterator iter);

  /**
   * \brief Get the `kind' (as in \RFC{793}) of this option
   * \return the Option Kind
   */
  virtual uint8_t GetKind (void) const;

  /**
   * \brief Returns number of bytes required for Option
   * serialization.
   *
   * \returns number of bytes required for Option
   * serialization
   */
  virtual uint32_t GetSerializedSize (void) const;

  /**
   * 
   */
  virtual void Print (std::ostream &os) const;

};

}