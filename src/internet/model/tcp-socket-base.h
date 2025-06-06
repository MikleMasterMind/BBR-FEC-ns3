/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */
#ifndef TCP_SOCKET_BASE_H
#define TCP_SOCKET_BASE_H

#include <stdint.h>
#include <queue>
#include <vector>
#include "ns3/traced-value.h"
#include "ns3/tcp-socket.h"
#include "ns3/ipv4-header.h"
#include "ns3/ipv6-header.h"
#include "ns3/timer.h"
#include "ns3/sequence-number.h"
#include "ns3/data-rate.h"
#include "ns3/node.h"
#include "tcp-rx-buffer.h"
#include "tcp-tx-buffer.h"
#include "rtt-estimator.h"
#include "tcp-l4-protocol.h"
// my code
#include "fec-module-encoder.h"
#include "fec-module-decoder.h"

namespace ns3 {


class Ipv4EndPoint;
class Ipv6EndPoint;
class Node;
class Packet;
class TcpL4Protocol;
class TcpHeader;
class TcpCongestionOps;
class RttEstimator;
class TcpRxBuffer;
class TcpTxBuffer;
class TcpOption;
class Ipv4Interface;
class Ipv6Interface;

/**
 * \ingroup tcp
 *
 * \brief Helper class to store RTT measurements
 */
class RttHistory
{
public:
  /**
   * \brief Constructor - builds an RttHistory with the given parameters
   * \param s First sequence number in packet sent
   * \param c Number of bytes sent
   * \param t Time this one was sent
   */
  RttHistory (SequenceNumber32 s, uint32_t c, Time t);
  /**
   * \brief Copy constructor
   * \param h the object to copy
   */
  RttHistory (const RttHistory& h); // Copy constructor
public:
  SequenceNumber32  seq;  //!< First sequence number in packet sent
  uint32_t        count;  //!< Number of bytes sent
  Time            time;   //!< Time this one was sent
  bool            retx;   //!< True if this has been retransmitted
};

struct PerPacketState
{
  uint64_t        m_delivered;
  Time            m_deliveredTime;
  Time            m_firstSentTime;
  uint32_t        m_isAppLimited;
  Time            m_sentTime;
  uint32_t        m_size;
  Time            m_lastSent;
};

typedef std::map<SequenceNumber32, struct PerPacketState> PerPacketState_t;

struct RateSample
{
  DataRate      m_deliveryRate;   //!< The delivery rate sample
  uint32_t      m_isAppLimited;   //!< Indicates whether the rate sample is application-limited
  Time          m_interval;       //!< The length of the sampling interval
  uint32_t      m_delivered;      //!< The amount of data marked as delivered over the sampling interval
  uint32_t      m_deliveredEce;   //!< The amount of data marked as delivered with an ECN mark over the sampling interval
  uint32_t      m_priorDelivered; //!< The delivered count of the most recent packet delivered
  uint32_t      m_priorDeliveredEce; //!< The delivered count of the most recent packet with ECN mark delivered
  Time          m_priorTime;      //!< The delivered time of the most recent packet delivered
  Time          m_sendElapsed;    //!< Send time interval calculated from the most recent packet delivered
  Time          m_ackElapsed;     //!< ACK time interval calculated from the most recent packet delivered
  uint32_t      m_lastAckedSackedBytes;   //!< Size of data sacked in the last ack
  uint32_t      m_packetLoss;
};

/**
 * \brief Data structure that records the congestion state of a connection
 *
 * In this data structure, basic informations that should be passed between
 * socket and the congestion control algorithm are saved. Through the code,
 * it will be referred as Transmission Control Block (TCB), but there are some
 * differencies. In the RFCs, the TCB contains all the variables that defines
 * a connection, while we preferred to maintain in this class only the values
 * that should be exchanged between socket and other parts, like congestion
 * control algorithms.
 *
 */
class TcpSocketState : public Object
{
public:
  /**
   * Get the type ID.
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief TcpSocketState Constructor
   */
  TcpSocketState () : Object () { }

  /**
   * \brief Copy constructor.
   * \param other object to copy.
   */
  TcpSocketState (const TcpSocketState &other);

  /**
   * \brief Definition of the Congestion state machine
   *
   * The design of this state machine is taken from Linux v4.0, but it has been
   * maintained in the Linux mainline from ages. It basically avoids to maintain
   * a lot of boolean variables, and it allows to check the transitions from
   * different algorithm in a cleaner way.
   *
   * These states represent the situation from a congestion control point of view:
   * in fact, apart the CA_OPEN state, the other states represent a situation in
   * which there is a congestion, and different actions should be taken,
   * depending on the case.
   *
   */
  typedef enum
  {
    CA_OPEN,      /**< Normal state, no dubious events */
    CA_DISORDER,  /**< In all the respects it is "Open",
                    *  but requires a bit more attention. It is entered when
                    *  we see some SACKs or dupacks. It is split of "Open" */
    CA_CWR,       /**< cWnd was reduced due to some Congestion Notification event.
                    *  It can be ECN, ICMP source quench, local device congestion.
                    *  Not used in NS-3 right now. */
    CA_RECOVERY,  /**< CWND was reduced, we are fast-retransmitting. */
    CA_LOSS,      /**< CWND was reduced due to RTO timeout or SACK reneging. */
    CA_LAST_STATE /**< Used only in debug messages */
  } TcpCongState_t;

  // Note: "not triggered" events are currently not triggered by the code.
  typedef enum
  {
    CA_EVENT_TX_START,     /**< first transmit when no packets in flight */
    CA_EVENT_CWND_RESTART, /**< congestion window restart. Not triggered */
    CA_EVENT_COMPLETE_CWR, /**< end of congestion recovery */
    CA_EVENT_LOSS,         /**< loss timeout */
    CA_EVENT_ECN_NO_CE,    /**< ECT set, but not CE marked. Not triggered */
    CA_EVENT_ECN_IS_CE,    /**< received CE marked IP packet. Not triggered */
    CA_EVENT_DELAYED_ACK,  /**< Delayed ack is sent */
    CA_EVENT_NON_DELAYED_ACK, /**< Non-delayed ack is sent */
    CA_EVENT_ECN_ECE_RCVD  /**< ECE received in ACK */
  } TcpCAEvent_t;

  /**
   * \brief Definition of the Ecn state machine
   * 
   */
  typedef enum
  {
    ECN_DISABLED = 0, /**< ECN disabled traffic                                                                          */
    ECN_IDLE,         /**< ECN is enabled  but currently there is no action pertaining to ECE or CWR to be taken         */
    ECN_CE_RCVD,      /**< Last packet received had CE bit set in IP header                                              */
    ECN_SENDING_ECE,  /**< Receiver sends an ACK with ECE bit set in TCP header                                          */
    ECN_ECE_RCVD,     /**< Last ACK received had ECE bit set in TCP header                                               */
    ECN_CWR_SENT      /**< Sender has reduced the congestion window, and sent a packet with CWR bit set in TCP header.
                        *  This state is used for tracing.                                                               */
  } EcnState_t;

  /**
   * \ingroup tcp
   * TracedValue Callback signature for TcpCongState_t
   *
   * \param [in] oldValue original value of the traced variable
   * \param [in] newValue new value of the traced variable
   */
  typedef void (* TcpCongStatesTracedValueCallback)(const TcpCongState_t oldValue,
                                                    const TcpCongState_t newValue);

  /**
   * \ingroup tcp
   * TracedValue Callback signature for EcnState_t
   * 
   * \param [in] oldValue original value of the traced variable
   * \param [in] newValue new value of the traced variable
   */
  typedef void (* EcnStatesTracedValueCallback)(const EcnState_t oldValue,
                                                const EcnState_t newValue);

  /**
   * \brief Literal names of TCP states for use in log messages
   */
  static const char* const TcpCongStateName[TcpSocketState::CA_LAST_STATE];

  /**
   * \brief Literal names of ECN states for use in log messages
   */
  static const char* const EcnStateName[TcpSocketState::ECN_CWR_SENT + 1];

  // Congestion control
  TracedValue<uint32_t>  m_cWnd;            //!< Congestion window
  TracedValue<uint32_t>  m_ssThresh;        //!< Slow start threshold
  uint32_t               m_initialCWnd;     //!< Initial cWnd value
  uint32_t               m_initialSsThresh; //!< Initial Slow Start Threshold value

  // Segment
  uint32_t               m_segmentSize   {0}; //!< Segment size
  SequenceNumber32       m_lastAckedSeq  {0}; //!< Last sequence ACKed
  SequenceNumber32       m_lastSentSeq   {0}; //!< Last sequence Sent

  TracedValue<TcpCongState_t> m_congState {CA_OPEN}; //!< State in the Congestion state machine

  TracedValue<EcnState_t> m_ecnState {ECN_DISABLED}; //!< Current ECN State, represented as combination of EcnState values

  TracedValue<SequenceNumber32> m_highTxMark     {0}; //!< Highest seqno ever sent, regardless of ReTx
  TracedValue<SequenceNumber32> m_nextTxSequence {0}; //!< Next seqnum to be sent (SND.NXT), ReTx pushes it back

  uint32_t               m_rcvTimestampValue     {0}; //!< Receiver Timestamp value
  uint32_t               m_rcvTimestampEchoReply {0}; //!< Sender Timestamp echoed by the receiver

  // Pacing related variables
  bool                   m_pacing            {false};//!< Pacing status
  DataRate               m_maxPacingRate     {0};    //!< Max Pacing rate
  DataRate               m_currentPacingRate {0};    //!< Current Pacing rate

  Time                   m_minRtt  {Time::Max ()};   //!< Minimum RTT observed throughout the connection
  Time                   m_lastRtt {Time::Max ()};   //!< Last RTT observed
  Time                   m_rttVar  {Time::Max ()};   //!< Variation in RTT
  uint32_t               m_bytesInFlight     {0};    //!< Bytes in flight
  uint32_t               m_priorInFlight     {0};    //!< Prior Bytes in flight

  uint64_t               m_delivered		    {0};           //!< The total amount of data in bytes delivered so far
  Time                   m_deliveredTime    {Seconds (0)}; //!< Simulator time when m_delivered was last updated
  Time                   m_firstSentTime    {Seconds (0)}; //!< The send time of the packet that was most recently marked as delivered
  uint32_t               m_appLimited		    {0};           //!< The index of the last transmitted packet marked as application-limited
  uint32_t               m_lastAckedSackedBytes {0};       //!< Size of data sacked in the last ack
  uint32_t               m_txItemDelivered      {0};

  struct RateSample      m_rs;

  /**
   * \brief Get cwnd in segments rather than bytes
   *
   * \return Congestion window in segments
   */
  uint32_t GetCwndInSegments () const
  {
    return m_cWnd / m_segmentSize;
  }

  /**
   * \brief Get slow start thresh in segments rather than bytes
   *
   * \return Slow start threshold in segments
   */
  uint32_t GetSsThreshInSegments () const
  {
    return m_ssThresh / m_segmentSize;
  }
};

/**
 * \ingroup socket
 * \ingroup tcp
 *
 * \brief A base class for implementation of a stream socket using TCP.
 *
 * This class contains the essential components of TCP, as well as a sockets
 * interface for upper layers to call. This class provides connection orientation
 * and sliding window flow control; congestion control is delegated to subclasses
 * of TcpCongestionOps. Part of TcpSocketBase is modified from the original
 * NS-3 TCP socket implementation (TcpSocketImpl) by
 * Raj Bhattacharjea <raj.b@gatech.edu> of Georgia Tech.
 *
 * For IPv4 packets, the TOS set for the socket is used. The Bind and Connect
 * operations set the TOS for the socket to the value specified in the provided
 * address. A SocketIpTos tag is only added to the packet if the resulting
 * TOS is non-null.
 * Each packet is assigned the priority set for the socket. Setting a TOS
 * for a socket also sets a priority for the socket (according to the
 * Socket::IpTos2Priority function). A SocketPriority tag is only added to the
 * packet if the priority is non-null.
 *
 * Congestion state machine
 * ---------------------------
 *
 * The socket maintains two state machines; the TCP one, and another called
 * "Congestion state machine", which keeps track of the phase we are in. Currently,
 * ns-3 manages the states:
 *
 * - CA_OPEN
 * - CA_DISORDER
 * - CA_RECOVERY
 * - CA_LOSS
 *
 * Another one (CA_CWR) is present but not used. For more information, see
 * the TcpCongState_t documentation.
 *
 * Congestion control interface
 * ---------------------------
 *
 * Congestion control, unlike older releases of ns-3, has been split from
 * TcpSocketBase. In particular, each congestion control is now a subclass of
 * the main TcpCongestionOps class. Switching between congestion algorithm is
 * now a matter of setting a pointer into the TcpSocketBase class. The idea
 * and the interfaces are inspired by the Linux operating system, and in
 * particular from the structure tcp_congestion_ops.
 *
 * Transmission Control Block (TCB)
 * --------------------------------
 *
 * The variables needed to congestion control classes to operate correctly have
 * been moved inside the TcpSocketState class. It contains information on the
 * congestion window, slow start threshold, segment size and the state of the
 * Congestion state machine.
 *
 * To track the trace inside the TcpSocketState class, a "forward" technique is
 * used, which consists in chaining callbacks from TcpSocketState to TcpSocketBase
 * (see for example cWnd trace source).
 *
 * Fast retransmit
 * ----------------
 *
 * The fast retransmit enhancement is introduced in RFC 2581 and updated in
 * RFC 5681. It reduces the time a sender waits before retransmitting
 * a lost segment, through the assumption that if it receives a certain number
 * of duplicate ACKs, a segment has been lost and it can be retransmitted.
 * Usually, it is coupled with the Limited Transmit algorithm, defined in
 * RFC 3042.
 *
 * In ns-3, these algorithms are included in this class, and it is implemented inside
 * the ProcessAck method. The attribute which manages the number of dup ACKs
 * necessary to start the fast retransmit algorithm is named "ReTxThreshold",
 * and its default value is 3, while the Limited Transmit one can be enabled
 * by setting the attribute "LimitedTransmit" to true. Before entering the
 * recovery phase, the method EnterRecovery is called.
 *
 * Fast recovery
 * -------------
 *
 * The fast recovery algorithm is introduced RFC 2001, and it
 * avoids to reset cWnd to 1 segment after sensing a loss on the channel. Instead,
 * the slow start threshold is halved, and the cWnd is set equal to such value,
 * plus segments for the cWnd inflation.
 *
 * The algorithm is implemented in the ProcessAck method.
 *
 * RTO expiration
 * --------------
 *
 * When the Retransmission Time Out expires, the TCP faces a big performance
 * drop. The expiration event is managed in ReTxTimeout method, that basically
 * set the cWnd to 1 segment and start "from scratch" again.
 *
 * Options management
 * ------------------
 *
 * SYN and SYN-ACK options, which are allowed only at the beginning of the
 * connection, are managed in the DoForwardUp and SendEmptyPacket methods.
 * To read all others, we have set up a cycle inside ReadOptions. For adding
 * them, there is no a unique place, since the options (and the information
 * available to build them) are scattered around the code. For instance,
 * the SACK option is built in SendEmptyPacket only under certain conditions.
 *
 * SACK
 * ----
 *
 * The SACK generation/management is delegated to the buffer classes, namely
 * TcpTxBuffer and TcpRxBuffer. Please take a look on their documentation if
 * you need more informations.
 *
 */
class TcpSocketBase : public TcpSocket
{
public:
  /**
   * Get the type ID.
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  /**
   * \brief Get the instance TypeId
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId () const;

  /**
   * \brief TcpGeneralTest friend class (for tests).
   * \relates TcpGeneralTest
   */
  friend class TcpGeneralTest;

  /**
   * Create an unbound TCP socket
   */
  TcpSocketBase (void);

  /**
   * Clone a TCP socket, for use upon receiving a connection request in LISTEN state
   *
   * \param sock the original Tcp Socket
   */
  TcpSocketBase (const TcpSocketBase& sock);
  virtual ~TcpSocketBase (void);

  // Set associated Node, TcpL4Protocol, RttEstimator to this socket

  /**
   * \brief Set the associated node.
   * \param node the node
   */
  virtual void SetNode (Ptr<Node> node);

  /**
   * \brief Set the associated TCP L4 protocol.
   * \param tcp the TCP L4 protocol
   */
  virtual void SetTcp (Ptr<TcpL4Protocol> tcp);

  /**
   * \brief Set the associated RTT estimator.
   * \param rtt the RTT estimator
   */
  virtual void SetRtt (Ptr<RttEstimator> rtt);

  /**
   * \brief Sets the Minimum RTO.
   * \param minRto The minimum RTO.
   */
  void SetMinRto (Time minRto);

  /**
   * \brief Get the Minimum RTO.
   * \return The minimum RTO.
   */
  Time GetMinRto (void) const;

  /**
   * \brief Sets the Clock Granularity (used in RTO calcs).
   * \param clockGranularity The Clock Granularity
   */
  void SetClockGranularity (Time clockGranularity);

  /**
   * \brief Get the Clock Granularity (used in RTO calcs).
   * \return The Clock Granularity.
   */
  Time GetClockGranularity (void) const;

  /**
   * \brief Get a pointer to the Tx buffer
   * \return a pointer to the tx buffer
   */
  Ptr<TcpTxBuffer> GetTxBuffer (void) const;

  /**
   * \brief Get a pointer to the Rx buffer
   * \return a pointer to the rx buffer
   */
  Ptr<TcpRxBuffer> GetRxBuffer (void) const;

  /**
   * \brief Callback pointer for cWnd trace chaining
   */
  TracedCallback<uint32_t, uint32_t> m_cWndTrace;

  /**
   * \brief Callback pointer for ssTh trace chaining
   */
  TracedCallback<uint32_t, uint32_t> m_ssThTrace;

  /**
   * \brief Callback pointer for congestion state trace chaining
   */
  TracedCallback<TcpSocketState::TcpCongState_t, TcpSocketState::TcpCongState_t> m_congStateTrace;

  /**
   * \brief Callback pointer for high tx mark chaining
   */
  TracedCallback <SequenceNumber32, SequenceNumber32> m_highTxMarkTrace;

  /**
   * \brief Callback pointer for next tx sequence chaining
   */
  TracedCallback<SequenceNumber32, SequenceNumber32> m_nextTxSequenceTrace;

  /**
   * \brief Callback pointer for ECN state trace chaining
   */
  TracedCallback<TcpSocketState::EcnState_t, TcpSocketState::EcnState_t> m_ecnStateTrace;

  /**
   * \brief Callback function to hook to TcpSocketState congestion window
   * \param oldValue old cWnd value
   * \param newValue new cWnd value
   */
  void UpdateCwnd (uint32_t oldValue, uint32_t newValue);

  /**
   * \brief Callback function to hook to TcpSocketState slow start threshold
   * \param oldValue old ssTh value
   * \param newValue new ssTh value
   */
  void UpdateSsThresh (uint32_t oldValue, uint32_t newValue);

  /**
   * \brief Callback function to hook to TcpSocketState congestion state
   * \param oldValue old congestion state value
   * \param newValue new congestion state value
   */
  void UpdateCongState (TcpSocketState::TcpCongState_t oldValue,
                        TcpSocketState::TcpCongState_t newValue);

  /**
   * \brief Callback function to hook to TcpSocketState high tx mark
   * \param oldValue old high tx mark
   * \param newValue new high tx mark
   */
  void UpdateHighTxMark (SequenceNumber32 oldValue, SequenceNumber32 newValue);

  /**
   * \brief Callback function to hook to TcpSocketState next tx sequence
   * \param oldValue old nextTxSeq value
   * \param newValue new nextTxSeq value
   */
  void UpdateNextTxSequence (SequenceNumber32 oldValue, SequenceNumber32 newValue);

  /**
   * \brief Callback function to hook to EcnState state
   * \param oldValue old ecn state value
   * \param newValue new ecn state value
   */
  void UpdateEcnState (TcpSocketState::EcnState_t oldValue,
                       TcpSocketState::EcnState_t newValue);

  /**
   * \brief Install a congestion control algorithm on this socket
   *
   * \param algo Algorithm to be installed
   */
  void SetCongestionControlAlgorithm (Ptr<TcpCongestionOps> algo);

  /**
   * \brief Mark ECT(0)
   * 
   * \return TOS with ECT(0)
   */
  inline uint8_t MarkEcnEct0 (uint8_t tos) const
    {
      return ((tos & 0xfc) | 0x02);
    }

   /**
   * \brief Mark ECT(1)
   *
   * \return TOS with ECT(1)
   */
  inline uint8_t MarkEcnEct1 (uint8_t tos) const
    {
      return ((tos & 0xfc) | 0x01);
    }

  /**
   * \brief Mark CE
   *
   * \return TOS with CE
   */
  inline uint8_t MarkEcnCe (uint8_t tos) const
    {
      return ((tos & 0xfc) | 0x03);
    }

  /**
   * \brief Clears ECN bits from TOS
   *
   * \return TOS without ECN bits
   */
  inline uint8_t ClearEcnBits (uint8_t tos) const
    {
      return tos & 0xfc;
    }

  /**
   * \brief ECN Modes
   */
  typedef enum
    {
      NoEcn = 0,   //!< ECN is not enabled.
      ClassicEcn   //!< ECN functionality as described in RFC 3168.
    } EcnMode_t;

  /**
   * \brief Checks if TOS has no ECN bits
   *
   * \return true if TOS does not have any ECN bits set; otherwise false
   */
  inline bool CheckNoEcn (uint8_t tos) const
    {
      return ((tos & 0xfc) == 0x00);
    }

  /**
   * \brief Checks for ECT(0) bits
   *
   * \return true if TOS has ECT(0) bit set; otherwise false
   */
  inline bool CheckEcnEct0 (uint8_t tos) const
    {
      return ((tos & 0xfc) == 0x02);
    }

  /**
   * \brief Checks for ECT(1) bits
   *
   * \return true if TOS has ECT(1) bit set; otherwise false
   */
  inline bool CheckEcnEct1 (uint8_t tos) const
    {
      return ((tos & 0xfc) == 0x01);
    }

  /**
   * \brief Checks for CE bits
   *
   * \return true if TOS has CE bit set; otherwise false
   */
  inline bool CheckEcnCe (uint8_t tos) const
    {
      return ((tos & 0xfc) == 0x03);
    }

  /**
   * \brief Set ECN mode to use on the socket
   *
   * \param ecnMode Mode of ECN. Currently NoEcn and ClassicEcn is supported.
   */
  void SetEcn (EcnMode_t ecnMode);

  // Necessary implementations of null functions from ns3::Socket
  virtual enum SocketErrno GetErrno (void) const;    // returns m_errno
  virtual enum SocketType GetSocketType (void) const; // returns socket type
  virtual Ptr<Node> GetNode (void) const;            // returns m_node
  virtual int Bind (void);    // Bind a socket by setting up endpoint in TcpL4Protocol
  virtual int Bind6 (void);    // Bind a socket by setting up endpoint in TcpL4Protocol
  virtual int Bind (const Address &address);         // ... endpoint of specific addr or port
  virtual int Connect (const Address &address);      // Setup endpoint and call ProcessAction() to connect
  virtual int Listen (void);  // Verify the socket is in a correct state and call ProcessAction() to listen
  virtual int Close (void);   // Close by app: Kill socket upon tx buffer emptied
  virtual int ShutdownSend (void);    // Assert the m_shutdownSend flag to prevent send to network
  virtual int ShutdownRecv (void);    // Assert the m_shutdownRecv flag to prevent forward to app
  virtual int Send (Ptr<Packet> p, uint32_t flags);  // Call by app to send data to network
  virtual int SendTo (Ptr<Packet> p, uint32_t flags, const Address &toAddress); // Same as Send(), toAddress is insignificant
  virtual Ptr<Packet> Recv (uint32_t maxSize, uint32_t flags); // Return a packet to be forwarded to app
  virtual Ptr<Packet> RecvFrom (uint32_t maxSize, uint32_t flags, Address &fromAddress); // ... and write the remote address at fromAddress
  virtual uint32_t GetTxAvailable (void) const; // Available Tx buffer size
  virtual uint32_t GetRxAvailable (void) const; // Available-to-read data size, i.e. value of m_rxAvailable
  virtual int GetSockName (Address &address) const; // Return local addr:port in address
  virtual int GetPeerName (Address &address) const;
  virtual void BindToNetDevice (Ptr<NetDevice> netdevice); // NetDevice with my m_endPoint

  /**
   * TracedCallback signature for tcp packet transmission or reception events.
   *
   * \param [in] packet The packet.
   * \param [in] header The TcpHeader
   * \param [in] socket This socket
   */
  typedef void (* TcpTxRxTracedCallback)(const Ptr<const Packet> packet, const TcpHeader& header,
                                         const Ptr<const TcpSocketBase> socket);

protected:
  // Implementing ns3::TcpSocket -- Attribute get/set
  // inherited, no need to doc

  virtual void     SetSndBufSize (uint32_t size);
  virtual uint32_t GetSndBufSize (void) const;
  virtual void     SetRcvBufSize (uint32_t size);
  virtual uint32_t GetRcvBufSize (void) const;
  virtual void     SetSegSize (uint32_t size);
  virtual uint32_t GetSegSize (void) const;
  virtual void     SetInitialSSThresh (uint32_t threshold);
  virtual uint32_t GetInitialSSThresh (void) const;
  virtual void     SetInitialCwnd (uint32_t cwnd);
  virtual uint32_t GetInitialCwnd (void) const;
  virtual void     SetConnTimeout (Time timeout);
  virtual Time     GetConnTimeout (void) const;
  virtual void     SetSynRetries (uint32_t count);
  virtual uint32_t GetSynRetries (void) const;
  virtual void     SetDataRetries (uint32_t retries);
  virtual uint32_t GetDataRetries (void) const;
  virtual void     SetDelAckTimeout (Time timeout);
  virtual Time     GetDelAckTimeout (void) const;
  virtual void     SetDelAckMaxCount (uint32_t count);
  virtual uint32_t GetDelAckMaxCount (void) const;
  virtual void     SetTcpNoDelay (bool noDelay);
  virtual bool     GetTcpNoDelay (void) const;
  virtual void     SetPersistTimeout (Time timeout);
  virtual Time     GetPersistTimeout (void) const;
  virtual bool     SetAllowBroadcast (bool allowBroadcast);
  virtual bool     GetAllowBroadcast (void) const;



  // Helper functions: Connection set up

  /**
   * \brief Common part of the two Bind(), i.e. set callback and remembering local addr:port
   *
   * \returns 0 on success, -1 on failure
   */
  int SetupCallback (void);

  /**
   * \brief Perform the real connection tasks: Send SYN if allowed, RST if invalid
   *
   * \returns 0 on success
   */
  int DoConnect (void);

  /**
   * \brief Schedule-friendly wrapper for Socket::NotifyConnectionSucceeded()
   */
  void ConnectionSucceeded (void);

  /**
   * \brief Configure the endpoint to a local address. Called by Connect() if Bind() didn't specify one.
   *
   * \returns 0 on success
   */
  int SetupEndpoint (void);

  /**
   * \brief Configure the endpoint v6 to a local address. Called by Connect() if Bind() didn't specify one.
   *
   * \returns 0 on success
   */
  int SetupEndpoint6 (void);

  /**
   * \brief Complete a connection by forking the socket
   *
   * This function is called only if a SYN received in LISTEN state. After
   * TcpSocketBase cloned, allocate a new end point to handle the incoming
   * connection and send a SYN+ACK to complete the handshake.
   *
   * \param p the packet triggering the fork
   * \param tcpHeader the TCP header of the triggering packet
   * \param fromAddress the address of the remote host
   * \param toAddress the address the connection is directed to
   */
  virtual void CompleteFork (Ptr<Packet> p, const TcpHeader& tcpHeader,
                             const Address& fromAddress, const Address& toAddress);



  // Helper functions: Transfer operation

  /**
   * \brief Called by the L3 protocol when it received a packet to pass on to TCP.
   *
   * \param packet the incoming packet
   * \param header the packet's IPv4 header
   * \param port the remote port
   * \param incomingInterface the incoming interface
   */
  void ForwardUp (Ptr<Packet> packet, Ipv4Header header, uint16_t port, Ptr<Ipv4Interface> incomingInterface);

  /**
   * \brief Called by the L3 protocol when it received a packet to pass on to TCP.
   *
   * \param packet the incoming packet
   * \param header the packet's IPv6 header
   * \param port the remote port
   * \param incomingInterface the incoming interface
   */
  void ForwardUp6 (Ptr<Packet> packet, Ipv6Header header, uint16_t port, Ptr<Ipv6Interface> incomingInterface);

  /**
   * \brief Called by TcpSocketBase::ForwardUp{,6}().
   *
   * Get a packet from L3. This is the real function to handle the
   * incoming packet from lower layers. This is
   * wrapped by ForwardUp() so that this function can be overloaded by daughter
   * classes.
   *
   * \param packet the incoming packet
   * \param fromAddress the address of the sender of packet
   * \param toAddress the address of the receiver of packet (hopefully, us)
   */
  virtual void DoForwardUp (Ptr<Packet> packet, const Address &fromAddress,
                            const Address &toAddress);

  /**
   * \brief Called by the L3 protocol when it received an ICMP packet to pass on to TCP.
   *
   * \param icmpSource the ICMP source address
   * \param icmpTtl the ICMP Time to Live
   * \param icmpType the ICMP Type
   * \param icmpCode the ICMP Code
   * \param icmpInfo the ICMP Info
   */
  void ForwardIcmp (Ipv4Address icmpSource, uint8_t icmpTtl, uint8_t icmpType, uint8_t icmpCode, uint32_t icmpInfo);

  /**
   * \brief Called by the L3 protocol when it received an ICMPv6 packet to pass on to TCP.
   *
   * \param icmpSource the ICMP source address
   * \param icmpTtl the ICMP Time to Live
   * \param icmpType the ICMP Type
   * \param icmpCode the ICMP Code
   * \param icmpInfo the ICMP Info
   */
  void ForwardIcmp6 (Ipv6Address icmpSource, uint8_t icmpTtl, uint8_t icmpType, uint8_t icmpCode, uint32_t icmpInfo);

  /**
   * \brief Check whether the given TCP Segment is valid or not.
   * 
   * \param seq the sequence number of packet's TCP header
   * \param tcpHeaderSize the size of packet's TCP header
   * \param tcpPayloadSize the size of TCP payload
   */
  bool IsValidTcpSegment (const SequenceNumber32 seq, const uint32_t  tcpHeaderSize,
                          const uint32_t tcpPayloadSize);

  /**
   * \brief Send as much pending data as possible according to the Tx window.
   *
   * Note that this function did not implement the PSH flag.
   *
   * \param withAck forces an ACK to be sent
   * \returns the number of packets sent
   */
  uint32_t SendPendingData (bool withAck = false);

  /**
   * \brief Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
   *        TCP header, and send to TcpL4Protocol
   *
   * \param seq the sequence numberdelete;
   * \param maxSize the maximum data block to be transmitted (in bytes)
   * \param withAck forces an ACK to be sent
   * \returns the number of bytes sent
   */
  virtual uint32_t SendDataPacket (SequenceNumber32 seq, uint32_t maxSize, bool withAck);
  
  // my code
  /**
   * 
   */
  virtual uint32_t SendFecPacket ();

  /**
   * \brief Send a empty packet that carries a flag, e.g., ACK
   *
   * \param flags the packet's flags
   */
  virtual void SendEmptyPacket (uint8_t flags);

  // my code
  /**
   * Send a empty packet with TcpOptionFec that carries a flag
   */
  virtual void SendEmptyFecPacket (uint8_t flags);

  /**
   * \brief Send reset and tear down this socket
   */
  void SendRST (void);

  /**
   * \brief Check if a sequence number range is within the rx window
   *
   * \param head start of the Sequence window
   * \param tail end of the Sequence window
   * \returns true if it is in range
   */
  bool OutOfRange (SequenceNumber32 head, SequenceNumber32 tail) const;


  // Helper functions: Connection close

  /**
   * \brief Close a socket by sending RST, FIN, or FIN+ACK, depend on the current state
   *
   * \returns 0 on success
   */
  int DoClose (void);

  /**
   * \brief Peacefully close the socket by notifying the upper layer and deallocate end point
   */
  void CloseAndNotify (void);

  /**
   * \brief Kill this socket by zeroing its attributes (IPv4)
   *
   * This is a callback function configured to m_endpoint in
   * SetupCallback(), invoked when the endpoint is destroyed.
   */
  void Destroy (void);

  /**
   * \brief Kill this socket by zeroing its attributes (IPv6)
   *
   * This is a callback function configured to m_endpoint in
   * SetupCallback(), invoked when the endpoint is destroyed.
   */
  void Destroy6 (void);

  /**
   * \brief Deallocate m_endPoint and m_endPoint6
   */
  void DeallocateEndPoint (void);

  /**
   * \brief Received a FIN from peer, notify rx buffer
   *
   * \param p the packet
   * \param tcpHeader the packet's TCP header
   */
  void PeerClose (Ptr<Packet> p, const TcpHeader& tcpHeader);

  /**
   * \brief FIN is in sequence, notify app and respond with a FIN
   */
  void DoPeerClose (void);

  /**
   * \brief Cancel all timer when endpoint is deleted
   */
  void CancelAllTimers (void);

  /**
   * \brief Move from CLOSING or FIN_WAIT_2 to TIME_WAIT state
   */
  void TimeWait (void);

  // State transition functions

  /**
   * \brief Received a packet upon ESTABLISHED state.
   *
   * This function is mimicking the role of tcp_rcv_established() in tcp_input.c in Linux kernel.
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  void ProcessEstablished (Ptr<Packet> packet, const TcpHeader& tcpHeader); // Received a packet upon ESTABLISHED state

  /**
   * \brief Received a packet upon LISTEN state.
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   * \param fromAddress the source address
   * \param toAddress the destination address
   */
  void ProcessListen (Ptr<Packet> packet, const TcpHeader& tcpHeader,
                      const Address& fromAddress, const Address& toAddress);

  /**
   * \brief Received a packet upon SYN_SENT
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  void ProcessSynSent (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * \brief Received a packet upon SYN_RCVD.
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   * \param fromAddress the source address
   * \param toAddress the destination address
   */
  void ProcessSynRcvd (Ptr<Packet> packet, const TcpHeader& tcpHeader,
                       const Address& fromAddress, const Address& toAddress);

  /**
   * \brief Received a packet upon CLOSE_WAIT, FIN_WAIT_1, FIN_WAIT_2
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  void ProcessWait (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * \brief Received a packet upon CLOSING
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  void ProcessClosing (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * \brief Received a packet upon LAST_ACK
   *
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  void ProcessLastAck (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  // Window management

  /**
   * \brief Return count of number of unacked bytes
   *
   * The difference between SND.UNA and HighTx
   *
   * \returns count of number of unacked bytes
   */
  virtual uint32_t UnAckDataCount (void) const;

  /**
   * \brief Return total bytes in flight
   *
   * Does not count segments lost and SACKed (or dupACKed)
   *
   * \returns total bytes in flight
   */
  virtual uint32_t BytesInFlight (void) const;

  /**
   * \brief Return the max possible number of unacked bytes
   * \returns the max possible number of unacked bytes
   */
  virtual uint32_t Window (void) const;

  /**
   * \brief Return unfilled portion of window
   * \return unfilled portion of window
   */
  virtual uint32_t AvailableWindow (void) const;

  /**
   * \brief The amount of Rx window announced to the peer
   * \param scale indicate if the window should be scaled. True for
   * almost all cases, except when we are sending a SYN
   * \returns size of Rx window announced to the peer
   */
  virtual uint16_t AdvertisedWindowSize (bool scale = true) const;

  /**
   * \brief Update the receiver window (RWND) based on the value of the
   * window field in the header.
   *
   * This method suppresses updates unless one of the following three
   * conditions holds:  1) segment contains new data (advancing the right
   * edge of the receive buffer), 2) segment does not contain new data
   * but the segment acks new data (highest sequence number acked advances),
   * or 3) the advertised window is larger than the current send window
   *
   * \param header TcpHeader from which to extract the new window value
   */
  void UpdateWindowSize (const TcpHeader& header);


  // Manage data tx/rx

  /**
   * \brief Call CopyObject<> to clone me
   * \returns a copy of the socket
   */
  virtual Ptr<TcpSocketBase> Fork (void);

  /**
   * \brief Received an ACK packet
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  virtual void ReceivedAck (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * \brief Process a received ack
   * \param ackNumber ack number
   * \param scoreboardUpdated if true indicates that the scoreboard has been
   * updated with SACK information
   */
  virtual void ProcessAck (const SequenceNumber32 &ackNumber, bool scoreboardUpdated);

  /**
   * \brief Recv of a data, put into buffer, call L7 to get it if necessary
   * \param packet the packet
   * \param tcpHeader the packet's TCP header
   */
  virtual void ReceivedData (Ptr<Packet> packet, const TcpHeader& tcpHeader);

  /**
   * \brief Take into account the packet for RTT estimation
   * \param tcpHeader the packet's TCP header
   */
  virtual void EstimateRtt (const TcpHeader& tcpHeader);

  /**
   * \brief Update the RTT history, when we send TCP segments
   *
   * \param seq The sequence number of the TCP segment
   * \param sz The segment's size
   * \param isRetransmission Whether or not the segment is a retransmission
   */

  virtual void UpdateRttHistory (const SequenceNumber32 &seq, uint32_t sz,
                                 bool isRetransmission);

  /**
   * \brief Update buffers w.r.t. ACK
   * \param seq the sequence number
   * \param resetRTO indicates if RTO should be reset
   */
  virtual void NewAck (SequenceNumber32 const& seq, bool resetRTO);

  /**
   * \brief Dupack management
   */
  void DupAck ();

  /**
   * \brief Limited transmit algorithm
   */
  void LimitedTransmit ();

  /**
   * \brief Enter the CA_RECOVERY, and retransmit the head
   */
  void EnterRecovery ();

  /**
   * \brief An RTO event happened
   */
  virtual void ReTxTimeout (void);

  /**
   * \brief Action upon delay ACK timeout, i.e. send an ACK
   */
  virtual void DelAckTimeout (void);

  /**
   * \brief Timeout at LAST_ACK, close the connection
   */
  virtual void LastAckTimeout (void);

  /**
   * \brief Send 1 byte probe to get an updated window size
   */
  virtual void PersistTimeout (void);

  /**
   * \brief Retransmit the oldest packet
   */
  virtual void DoRetransmit (void);

  /** \brief Add options to TcpHeader
   *
   * Test each option, and if it is enabled on our side, add it
   * to the header
   *
   * \param tcpHeader TcpHeader to add options to
   */
  void AddOptions (TcpHeader& tcpHeader);

  /**
   * \brief Read TCP options begore Ack processing
   *
   * Timestamp and Window scale are managed in other pieces of code.
   *
   * \param tcpHeader Header of the segment
   * \param scoreboardUpdated indicates if the scoreboard was updated due to a
   * SACK option
   */
  void ReadOptions (const TcpHeader &tcpHeader, bool &scoreboardUpdated);

  /**
   * \brief Return true if the specified option is enabled
   *
   * \param kind kind of TCP option
   * \return true if the option is enabled
   */
  bool IsTcpOptionEnabled (uint8_t kind) const;

  /**
   * \brief Read and parse the Window scale option
   *
   * Read the window scale option (encoded logarithmically) and save it.
   * Per RFC 1323, the value can't exceed 14.
   *
   * \param option Window scale option read from the header
   */
  void ProcessOptionWScale (const Ptr<const TcpOption> option);
  /**
   * \brief Add the window scale option to the header
   *
   * Calculate our factor from the rxBuffer max size, and add it
   * to the header.
   *
   * \param header TcpHeader where the method should add the window scale option
   */
  void AddOptionWScale (TcpHeader& header);

  /**
   * \brief Calculate window scale value based on receive buffer space
   *
   * Calculate our factor from the rxBuffer max size
   *
   * \returns the Window Scale factor
   */
  uint8_t CalculateWScale () const;

  /**
   * \brief Read the SACK PERMITTED option
   *
   * Currently this is a placeholder, since no operations should be done
   * on such option.
   *
   * \param option SACK PERMITTED option from the header
   */
  void ProcessOptionSackPermitted (const Ptr<const TcpOption> option);

  /**
   * \brief Read the SACK option
   *
   * \param option SACK option from the header
   * \returns true in case of an update to the SACKed blocks
   */
  bool ProcessOptionSack (const Ptr<const TcpOption> option);

  /**
   * \brief Add the SACK PERMITTED option to the header
   *
   * \param header TcpHeader where the method should add the option
   */
  void AddOptionSackPermitted (TcpHeader &header);

  /**
   * \brief Add the SACK option to the header
   *
   * \param header TcpHeader where the method should add the option
   */
  void AddOptionSack (TcpHeader& header);

  /** \brief Process the timestamp option from other side
   *
   * Get the timestamp and the echo, then save timestamp (which will
   * be the echo value in our out-packets) and save the echoed timestamp,
   * to utilize later to calculate RTT.
   *
   * \see EstimateRtt
   * \param option Option from the segment
   * \param seq Sequence number of the segment
   */
  void ProcessOptionTimestamp (const Ptr<const TcpOption> option,
                               const SequenceNumber32 &seq);
  /**
   * \brief Add the timestamp option to the header
   *
   * Set the timestamp as the lower bits of the Simulator::Now time,
   * and the echo value as the last seen timestamp from the other part.
   *
   * \param header TcpHeader to which add the option to
   */
  void AddOptionTimestamp (TcpHeader& header);

  /**
   * \brief Performs a safe subtraction between a and b (a-b)
   *
   * Safe is used to indicate that, if b>a, the results returned is 0.
   *
   * \param a first number
   * \param b second number
   * \return 0 if b>0, (a-b) otherwise
   */
  static uint32_t SafeSubtraction (uint32_t a, uint32_t b);

  /**
   * \brief Notify Pacing
   */
  void NotifyPacingPerformed (void);

  /**
   * \brief Selects congestion ops method for cwnd adjustment
   *
   * This method determines whether to use CongControl or follow the
   * normal cwnd increase/decrease.
   *
   * \param segsAcked count of segments acked
   */
  void TcpCongControl (uint32_t segsAcked);

  /**
   * \brief Updates per packet variables required for rate sampling on each
   * packet transmission
   */
  void UpdatePacketSent (SequenceNumber32 seq, uint32_t sz);

  /**
   * \brief Calculates delivery rate on arrival or each acknowledged
   *
   * \returns True if delivery rate is updated
   */
  void UpdateRateSample (SequenceNumber32 seq);

  /**
   * \brief Calculates delivery rate on arrival or each acknowledged
   *
   * \returns True if delivery rate is updated
   */
  bool GenerateRateSample ();

  /**
   * \brief Checks if connection is app-limited upon each write from the application
   */
  void OnApplicationWrite ();
  
 /**
   * \brief Add Tags for the Socket
   * \param p Packet
   */
  void AddSocketTags (const Ptr<Packet> &p) const;

protected:
  // Counters and events
  EventId           m_retxEvent     {}; //!< Retransmission event
  EventId           m_lastAckEvent  {}; //!< Last ACK timeout event
  EventId           m_delAckEvent   {}; //!< Delayed ACK timeout event
  EventId           m_persistEvent  {}; //!< Persist event: Send 1 byte to probe for a non-zero Rx window
  EventId           m_timewaitEvent {}; //!< TIME_WAIT expiration event: Move this socket to CLOSED state

  // ACK management
  uint32_t          m_dupAckCount {0};     //!< Dupack counter
  uint32_t          m_delAckCount {0};     //!< Delayed ACK counter
  uint32_t          m_delAckMaxCount {0};  //!< Number of packet to fire an ACK before delay timeout

  // Nagle algorithm
  bool              m_noDelay {false};     //!< Set to true to disable Nagle's algorithm

  // Retries
  uint32_t          m_synCount     {0}; //!< Count of remaining connection retries
  uint32_t          m_synRetries   {0}; //!< Number of connection attempts
  uint32_t          m_dataRetrCount{0}; //!< Count of remaining data retransmission attempts
  uint32_t          m_dataRetries  {0}; //!< Number of data retransmission attempts

  // Timeouts
  TracedValue<Time> m_rto     {Seconds (0.0)}; //!< Retransmit timeout
  Time              m_minRto  {Time::Max()};   //!< minimum value of the Retransmit timeout
  TracedValue<Time> m_lastRtt {Seconds (0.0)}; //!< Last RTT sample collected
  Time              m_clockGranularity {Seconds (0.001)}; //!< Clock Granularity used in RTO calcs
  Time              m_delAckTimeout    {Seconds (0.0)};   //!< Time to delay an ACK
  Time              m_persistTimeout   {Seconds (0.0)};   //!< Time between sending 1-byte probes
  Time              m_cnTimeout        {Seconds (0.0)};   //!< Timeout for connection retry

  // History of RTT
  std::deque<RttHistory>      m_history;         //!< List of sent packet

  // Connections to other layers of TCP/IP
  Ipv4EndPoint*       m_endPoint  {nullptr}; //!< the IPv4 endpoint
  Ipv6EndPoint*       m_endPoint6 {nullptr}; //!< the IPv6 endpoint
  Ptr<Node>           m_node      {nullptr}; //!< the associated node
  Ptr<TcpL4Protocol>  m_tcp       {nullptr}; //!< the associated TCP L4 protocol
  Callback<void, Ipv4Address,uint8_t,uint8_t,uint8_t,uint32_t> m_icmpCallback;  //!< ICMP callback
  Callback<void, Ipv6Address,uint8_t,uint8_t,uint8_t,uint32_t> m_icmpCallback6; //!< ICMPv6 callback

  Ptr<RttEstimator> m_rtt {nullptr}; //!< Round trip time estimator

  // Rx and Tx buffer management
  Ptr<TcpRxBuffer> m_rxBuffer {nullptr}; //!< Rx buffer (reordering buffer)
  Ptr<TcpTxBuffer> m_txBuffer {nullptr}; //!< Tx buffer

  // State-related attributes
  TracedValue<TcpStates_t> m_state {CLOSED};         //!< TCP state
  mutable enum SocketErrno m_errno {ERROR_NOTERROR}; //!< Socket error code
  bool                     m_closeNotified {false};  //!< Told app to close socket
  bool                     m_closeOnEmpty  {false};  //!< Close socket upon tx buffer emptied
  bool                     m_shutdownSend  {false};  //!< Send no longer allowed
  bool                     m_shutdownRecv  {false};  //!< Receive no longer allowed
  bool                     m_connected     {false};  //!< Connection established
  double                   m_msl           {0.0};    //!< Max segment lifetime

  // Window management
  uint16_t         m_maxWinSize              {0};  //!< Maximum window size to advertise
  uint32_t         m_bytesAckedNotProcessed  {0};  //!< Bytes acked, but not processed
  SequenceNumber32 m_highTxAck               {0};  //!< Highest ack sent
  TracedValue<uint32_t> m_rWnd               {0};  //!< Receiver window (RCV.WND in RFC793)
  TracedValue<uint32_t> m_advWnd             {0};  //!< Advertised Window size
  TracedValue<SequenceNumber32> m_highRxMark {0};  //!< Highest seqno received
  TracedValue<SequenceNumber32> m_highRxAckMark {0}; //!< Highest ack received
  TracedValue<uint32_t>         m_bytesInFlight {0}; //!< Bytes in flight

  // Options
  bool    m_sackEnabled       {true}; //!< RFC SACK option enabled
  bool    m_winScalingEnabled {true}; //!< Window Scale option enabled (RFC 7323)
  uint8_t m_rcvWindShift      {0};    //!< Window shift to apply to outgoing segments
  uint8_t m_sndWindShift      {0};    //!< Window shift to apply to incoming segments
  bool     m_timestampEnabled {true}; //!< Timestamp option enabled
  uint32_t m_timestampToEcho  {0};    //!< Timestamp to echo

  EventId m_sendPendingDataEvent {}; //!< micro-delay event to send pending data

  // Fast Retransmit and Recovery
  SequenceNumber32       m_recover    {0};   //!< Previous highest Tx seqnum for fast recovery (set it to initial seq number)
  uint32_t               m_retxThresh {3};   //!< Fast Retransmit threshold
  bool                   m_limitedTx  {true};//!< perform limited transmit

  // Transmission Control Block
  Ptr<TcpSocketState>    m_tcb {nullptr};               //!< Congestion control informations
  Ptr<TcpCongestionOps>  m_congestionControl {nullptr}; //!< Congestion control

  // Guesses over the other connection end
  bool m_isFirstPartialAck {true}; //!< First partial ACK during RECOVERY

  // The following two traces pass a packet with a TCP header
  TracedCallback<Ptr<const Packet>, const TcpHeader&,
                 Ptr<const TcpSocketBase> > m_txTrace; //!< Trace of transmitted packets

  TracedCallback<Ptr<const Packet>, const TcpHeader&,
                 Ptr<const TcpSocketBase> > m_rxTrace; //!< Trace of received packets

  // Pacing related variable
  Timer m_pacingTimer {Timer::REMOVE_ON_DESTROY}; //!< Pacing Event

  PerPacketState_t  m_pps;

  // Parameters related to Explicit Congestion Notification
  EcnMode_t                     m_ecnMode    {EcnMode_t::NoEcn};  //!< Socket ECN capability
  TracedValue<SequenceNumber32> m_ecnEchoSeq {0};                 //!< Sequence number of the last received ECN Echo
  TracedValue<SequenceNumber32> m_ecnCESeq   {0};                 //!< Sequence number of the last received Congestion Experienced
  TracedValue<SequenceNumber32> m_ecnCWRSeq  {0};                 //!< Sequence number of the last sent CWR

  // forward error correction
  // my code
  Ptr<ForwardErrorCorrectionEncoder> m_fecEncoder;
  Ptr<ForwardErrorCorrectionDecoder> m_fecDecoder;
  SequenceNumber32 m_nextFecSeq;
  bool m_lossInBlock;
};

/**
 * \ingroup tcp
 * TracedValue Callback signature for TcpCongState_t
 *
 * \param [in] oldValue original value of the traced variable
 * \param [in] newValue new value of the traced variable
 */
typedef void (* TcpCongStatesTracedValueCallback)(const TcpSocketState::TcpCongState_t oldValue,
                                                  const TcpSocketState::TcpCongState_t newValue);

/**
 * \ingroup tcp
 * TracedValue Callback signature for EcnState_t
 *
 * \param [in] oldValue original value of the traced variable
 * \param [in] newValue new value of the traced variable
 */
typedef void (* EcnStatesTracedValueCallback)(const TcpSocketState::EcnState_t oldValue,
                                              const TcpSocketState::EcnState_t newValue);

} // namespace ns3

#endif /* TCP_SOCKET_BASE_H */

