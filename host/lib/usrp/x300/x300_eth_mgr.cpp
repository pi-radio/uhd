//
// Copyright 2019 Ettus Research, a National Instruments Brand
//
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "x300_eth_mgr.hpp"
#include "x300_claim.hpp"
#include "x300_fw_common.h"
#include "x300_mb_eeprom.hpp"
#include "x300_mb_eeprom_iface.hpp"
#include "x300_regs.hpp"
#include <uhd/exception.hpp>
#include <uhd/transport/if_addrs.hpp>
#include <uhd/transport/udp_constants.hpp>
#include <uhd/transport/udp_simple.hpp>
#include <uhd/transport/udp_zero_copy.hpp>
#include <uhd/transport/zero_copy_recv_offload.hpp>
#ifdef HAVE_DPDK
#    include <uhdlib/transport/dpdk_simple.hpp>
#    include <uhdlib/transport/dpdk_zero_copy.hpp>
#endif
#include <uhdlib/usrp/cores/i2c_core_100_wb32.hpp>
#include <boost/asio.hpp>
#include <string>

uhd::wb_iface::sptr x300_make_ctrl_iface_enet(
    uhd::transport::udp_simple::sptr udp, bool enable_errors = true);

using namespace uhd;
using namespace uhd::usrp;
using namespace uhd::transport;
using namespace uhd::usrp::x300;
namespace asio = boost::asio;

namespace {

constexpr unsigned int X300_UDP_RESERVED_FRAME_SIZE = 64;
// Reduced to make sure flow control packets are not blocked for too long at
// high rates:
constexpr size_t XGE_DATA_FRAME_SEND_SIZE = 4000;
constexpr size_t XGE_DATA_FRAME_RECV_SIZE = 8000;
constexpr size_t GE_DATA_FRAME_SEND_SIZE  = 1472;
constexpr size_t GE_DATA_FRAME_RECV_SIZE  = 1472;
constexpr size_t ETH_MSG_NUM_FRAMES       = 64;
constexpr size_t ETH_DATA_NUM_FRAMES      = 32;
constexpr size_t ETH_MSG_FRAME_SIZE       = uhd::transport::udp_simple::mtu; // bytes
constexpr size_t MAX_RATE_10GIGE          = (size_t)( // bytes/s
    10e9 / 8 * // wire speed multiplied by percentage of packets that is sample data
    (float(x300::DATA_FRAME_MAX_SIZE - uhd::usrp::DEVICE3_TX_MAX_HDR_LEN)
        / float(x300::DATA_FRAME_MAX_SIZE
                + 8 /* UDP header */ + 20 /* Ethernet header length */)));
constexpr size_t MAX_RATE_1GIGE           = (size_t)( // bytes/s
    10e9 / 8 * // wire speed multiplied by percentage of packets that is sample data
    (float(GE_DATA_FRAME_RECV_SIZE - uhd::usrp::DEVICE3_TX_MAX_HDR_LEN)
        / float(GE_DATA_FRAME_RECV_SIZE
                + 8 /* UDP header */ + 20 /* Ethernet header length */)));


} // namespace

/******************************************************************************
 * Static Methods
 *****************************************************************************/
eth_manager::udp_simple_factory_t eth_manager::x300_get_udp_factory(
    const device_addr_t& args)
{
    udp_simple_factory_t udp_make_connected = udp_simple::make_connected;
    if (args.has_key("use_dpdk")) {
#ifdef HAVE_DPDK
        udp_make_connected = [](const std::string& addr, const std::string& port) {
            auto& ctx = uhd::transport::uhd_dpdk_ctx::get();
            return dpdk_simple::make_connected(ctx, addr, port);
        };
#else
        UHD_LOG_WARNING(
            "DPDK", "Detected use_dpdk argument, but DPDK support not built in.");
#endif
    }
    return udp_make_connected;
}

device_addrs_t eth_manager::find(const device_addr_t& hint)
{
    udp_simple_factory_t udp_make_broadcast = udp_simple::make_broadcast;
    udp_simple_factory_t udp_make_connected = x300_get_udp_factory(hint);
#ifdef HAVE_DPDK
    if (hint.has_key("use_dpdk")) {
        auto& dpdk_ctx = uhd::transport::uhd_dpdk_ctx::get();
        if (not dpdk_ctx.is_init_done()) {
            dpdk_ctx.init(hint);
        }
        udp_make_broadcast = [](const std::string& addr, const std::string& port) {
            auto& ctx = uhd::transport::uhd_dpdk_ctx::get();
            return dpdk_simple::make_broadcast(ctx, addr, port);
        };
    }
#endif
    udp_simple::sptr comm =
        udp_make_broadcast(hint["addr"], BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT));

    // load request struct
    x300_fw_comms_t request = x300_fw_comms_t();
    request.flags           = uhd::htonx<uint32_t>(X300_FW_COMMS_FLAGS_ACK);
    request.sequence        = uhd::htonx<uint32_t>(std::rand());

    // send request
    comm->send(asio::buffer(&request, sizeof(request)));

    // loop for replies until timeout
    device_addrs_t addrs;
    while (true) {
        char buff[X300_FW_COMMS_MTU] = {};
        const size_t nbytes          = comm->recv(asio::buffer(buff), 0.050);
        if (nbytes == 0)
            break;
        const x300_fw_comms_t* reply = (const x300_fw_comms_t*)buff;
        if (request.flags != reply->flags)
            continue;
        if (request.sequence != reply->sequence)
            continue;
        device_addr_t new_addr;
        new_addr["type"] = "x300";
        new_addr["addr"] = comm->get_recv_addr();

        // Attempt to read the name from the EEPROM and perform filtering.
        // This operation can throw due to compatibility mismatch.
        try {
            wb_iface::sptr zpu_ctrl = x300_make_ctrl_iface_enet(
                udp_make_connected(
                    new_addr["addr"], BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT)),
                false /* Suppress timeout errors */
            );

            new_addr["fpga"] = get_fpga_option(zpu_ctrl);

            i2c_core_100_wb32::sptr zpu_i2c =
                i2c_core_100_wb32::make(zpu_ctrl, I2C1_BASE);
            x300_mb_eeprom_iface::sptr eeprom_iface =
                x300_mb_eeprom_iface::make(zpu_ctrl, zpu_i2c);
            const mboard_eeprom_t mb_eeprom = get_mb_eeprom(eeprom_iface);
            if (mb_eeprom.size() == 0 or claim_status(zpu_ctrl) == CLAIMED_BY_OTHER) {
                // Skip device claimed by another process
                continue;
            }
            new_addr["name"]   = mb_eeprom["name"];
            new_addr["serial"] = mb_eeprom["serial"];
            const std::string product_name =
                map_mb_type_to_product_name(get_mb_type_from_eeprom(mb_eeprom));
            if (!product_name.empty()) {
                new_addr["product"] = product_name;
            }
        } catch (const std::exception&) {
            // set these values as empty string so the device may still be found
            // and the filter's below can still operate on the discovered device
            new_addr["name"]   = "";
            new_addr["serial"] = "";
        }
        // filter the discovered device below by matching optional keys
        if ((not hint.has_key("name") or hint["name"] == new_addr["name"])
            and (not hint.has_key("serial") or hint["serial"] == new_addr["serial"])
            and (not hint.has_key("product") or hint["product"] == new_addr["product"])) {
            addrs.push_back(new_addr);
        }
    }

    return addrs;
}

/******************************************************************************
 * Structors
 *****************************************************************************/
eth_manager::eth_manager(const x300_device_args_t& args,
    uhd::property_tree::sptr tree,
    const uhd::fs_path& root_path)
    : _args(args)
{
    UHD_ASSERT_THROW(!args.get_first_addr().empty());

    auto dev_addr = args.get_orig_args();
    for (const std::string& key : dev_addr.keys()) {
        if (key.find("recv") != std::string::npos)
            recv_args[key] = dev_addr[key];
        if (key.find("send") != std::string::npos)
            send_args[key] = dev_addr[key];
    }

    // Initially store only the first address provided to setup communication
    // Once we read the EEPROM, we use it to map IP to its interface
    // In discover_eth(), we'll check and enable the other IP address, if given
    x300_eth_conn_t init;
    init.addr = args.get_first_addr();
    eth_conns.push_back(init);

    _x300_make_udp_connected = x300_get_udp_factory(dev_addr);

    tree->create<double>(root_path / "link_max_rate").set(10e9);
    _tree = tree->subtree(root_path);
}

both_xports_t eth_manager::make_transport(both_xports_t xports,
    const uhd::usrp::device3_impl::xport_type_t xport_type,
    const uhd::device_addr_t& args,
    const size_t send_mtu,
    const size_t recv_mtu,
    std::function<uhd::sid_t(uint32_t, uint32_t)>&& allocate_sid)
{
    zero_copy_xport_params default_buff_args;
    xports.endianness = ENDIANNESS_BIG;
    xports.lossless   = false;
    xports.recv       = nullptr;

    size_t& next_src_addr = xport_type == uhd::usrp::device3_impl::TX_DATA
                                ? _next_tx_src_addr
                                : xport_type == uhd::usrp::device3_impl::RX_DATA
                                      ? _next_rx_src_addr
                                      : _next_src_addr;

    // Decide on the IP/Interface pair based on the endpoint index
    x300_eth_conn_t conn         = eth_conns[next_src_addr];
    const uint32_t xbar_src_addr = next_src_addr == 0 ? x300::SRC_ADDR0 : x300::SRC_ADDR1;
    const uint32_t xbar_src_dst  = conn.type == X300_IFACE_ETH0 ? x300::XB_DST_E0
                                                               : x300::XB_DST_E1;

    // Do not increment src addr for tx_data by default, using dual ethernet
    // with the DMA FIFO causes sequence errors to DMA FIFO bandwidth
    // limitations.
    if (xport_type != uhd::usrp::device3_impl::TX_DATA
        || _args.get_enable_tx_dual_eth()) {
        next_src_addr = (next_src_addr + 1) % eth_conns.size();
    }

    xports.send_sid = allocate_sid(xbar_src_addr, xbar_src_dst);
    xports.recv_sid = xports.send_sid.reversed();
    // Set size and number of frames
    default_buff_args.send_frame_size = std::min(send_mtu, ETH_MSG_FRAME_SIZE);
    default_buff_args.recv_frame_size = std::min(recv_mtu, ETH_MSG_FRAME_SIZE);

    if (_args.get_use_dpdk()) {
#ifdef HAVE_DPDK
        auto& dpdk_ctx = uhd::transport::uhd_dpdk_ctx::get();

        default_buff_args.num_recv_frames = ETH_MSG_NUM_FRAMES;
        default_buff_args.num_send_frames = ETH_MSG_NUM_FRAMES;
        if (xport_type == uhd::usrp::device3_impl::CTRL) {
            // Increasing number of recv frames here because ctrl_iface uses it
            // to determine how many control packets can be in flight before it
            // must wait for an ACK
            default_buff_args.num_recv_frames =
                uhd::rfnoc::CMD_FIFO_SIZE / uhd::rfnoc::MAX_CMD_PKT_SIZE;
        } else if (xport_type == uhd::usrp::device3_impl::TX_DATA) {
            size_t default_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                            ? GE_DATA_FRAME_SEND_SIZE
                                            : XGE_DATA_FRAME_SEND_SIZE;
            default_buff_args.send_frame_size = args.cast<size_t>(
                "send_frame_size", std::min(default_frame_size, send_mtu));
            default_buff_args.num_send_frames =
                args.cast<size_t>("num_send_frames", default_buff_args.num_send_frames);
            default_buff_args.send_buff_size = args.cast<size_t>("send_buff_size", 0);
        } else if (xport_type == uhd::usrp::device3_impl::RX_DATA) {
            size_t default_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                            ? GE_DATA_FRAME_RECV_SIZE
                                            : XGE_DATA_FRAME_RECV_SIZE;
            default_buff_args.recv_frame_size = args.cast<size_t>(
                "recv_frame_size", std::min(default_frame_size, recv_mtu));
            default_buff_args.num_recv_frames =
                args.cast<size_t>("num_recv_frames", default_buff_args.num_recv_frames);
            default_buff_args.recv_buff_size = args.cast<size_t>("recv_buff_size", 0);
        }

        int dpdk_port_id = dpdk_ctx.get_route(conn.addr);
        if (dpdk_port_id < 0) {
            throw uhd::runtime_error(
                "Could not find a DPDK port with route to " + conn.addr);
        }
        auto recv = transport::dpdk_zero_copy::make(dpdk_ctx,
            (const unsigned int)dpdk_port_id,
            conn.addr,
            BOOST_STRINGIZE(X300_VITA_UDP_PORT),
            "0",
            default_buff_args,
            uhd::device_addr_t());

        xports.recv = recv; // Note: This is a type cast!
        xports.send = xports.recv;
        xports.recv_buff_size =
            (default_buff_args.recv_frame_size - X300_UDP_RESERVED_FRAME_SIZE)
            * default_buff_args.num_recv_frames;
        xports.send_buff_size =
            (default_buff_args.send_frame_size - X300_UDP_RESERVED_FRAME_SIZE)
            * default_buff_args.num_send_frames;
        UHD_LOG_TRACE("BUFF",
            "num_recv_frames="
                << default_buff_args.num_recv_frames
                << ", num_send_frames=" << default_buff_args.num_send_frames
                << ", recv_frame_size=" << default_buff_args.recv_frame_size
                << ", send_frame_size=" << default_buff_args.send_frame_size);

#else
        UHD_LOG_WARNING("X300", "Cannot create DPDK transport, falling back to UDP");
#endif
    }
    if (!xports.recv) {
        // Buffering is done in the socket buffers, so size them relative to
        // the link rate
        default_buff_args.send_buff_size = conn.link_rate / 50; // 20ms
        default_buff_args.recv_buff_size = std::max(conn.link_rate / 50,
            ETH_MSG_NUM_FRAMES * ETH_MSG_FRAME_SIZE); // enough to hold greater of 20ms or
                                                      // number of msg frames
        // There is no need for more than 1 send and recv frame since the
        // buffering is done in the socket buffers
        default_buff_args.num_send_frames = 1;
        default_buff_args.num_recv_frames = 1;
        if (xport_type == uhd::usrp::device3_impl::CTRL) {
            // Increasing number of recv frames here because ctrl_iface uses it
            // to determine how many control packets can be in flight before it
            // must wait for an ACK
            default_buff_args.num_recv_frames =
                uhd::rfnoc::CMD_FIFO_SIZE / uhd::rfnoc::MAX_CMD_PKT_SIZE;
        } else if (xport_type == uhd::usrp::device3_impl::TX_DATA) {
            size_t default_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                            ? GE_DATA_FRAME_SEND_SIZE
                                            : XGE_DATA_FRAME_SEND_SIZE;
            default_buff_args.send_frame_size = args.cast<size_t>(
                "send_frame_size", std::min(default_frame_size, send_mtu));
            default_buff_args.num_send_frames =
                args.cast<size_t>("num_send_frames", default_buff_args.num_send_frames);
            default_buff_args.send_buff_size =
                args.cast<size_t>("send_buff_size", default_buff_args.send_buff_size);
        } else if (xport_type == uhd::usrp::device3_impl::RX_DATA) {
            size_t default_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                            ? GE_DATA_FRAME_RECV_SIZE
                                            : XGE_DATA_FRAME_RECV_SIZE;
            default_buff_args.recv_frame_size = args.cast<size_t>(
                "recv_frame_size", std::min(default_frame_size, recv_mtu));
            // set some buffers so the offload thread actually offloads the
            // socket I/O
            default_buff_args.num_recv_frames = args.cast<size_t>("num_recv_frames", 2);
            default_buff_args.recv_buff_size =
                args.cast<size_t>("recv_buff_size", default_buff_args.recv_buff_size);
        }

        // make a new transport - fpga has no idea how to talk to us on this yet
        udp_zero_copy::buff_params buff_params;
        xports.recv = udp_zero_copy::make(conn.addr,
            BOOST_STRINGIZE(X300_VITA_UDP_PORT),
            default_buff_args,
            buff_params);

        // Create a threaded transport for the receive chain only
        if (xport_type == uhd::usrp::device3_impl::RX_DATA) {
            xports.recv = zero_copy_recv_offload::make(
                xports.recv, x300::RECV_OFFLOAD_BUFFER_TIMEOUT);
        }

        xports.send = xports.recv;

        // For the UDP transport the buffer size is the size of the socket buffer
        // in the kernel
        xports.recv_buff_size = buff_params.recv_buff_size;
        xports.send_buff_size = buff_params.send_buff_size;
    }

    // send a mini packet with SID into the ZPU
    // ZPU will reprogram the ethernet framer
    UHD_LOGGER_DEBUG("X300") << "programming packet for new xport on " << conn.addr
                             << " sid " << xports.send_sid;
    // YES, get a __send__ buffer from the __recv__ socket
    //-- this is the only way to program the framer for recv:
    managed_send_buffer::sptr buff = xports.recv->get_send_buff();
    buff->cast<uint32_t*>()[0]     = 0; // eth dispatch looks for != 0
    buff->cast<uint32_t*>()[1]     = uhd::htonx(xports.send_sid.get());
    buff->commit(8);
    buff.reset();

    return xports;
}

/******************************************************************************
 * API
 *****************************************************************************/
wb_iface::sptr eth_manager::get_ctrl_iface()
{
    return x300_make_ctrl_iface_enet(_x300_make_udp_connected(
        get_pri_eth().addr, BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT)));
}

void eth_manager::init_link(
    const mboard_eeprom_t& mb_eeprom, const std::string& loaded_fpga_image)
{
    double link_max_rate = 0.0;

    // Discover ethernet interfaces
    discover_eth(mb_eeprom, loaded_fpga_image);

    /* This is an ETH connection. Figure out what the maximum supported frame
     * size is for the transport in the up and down directions. The frame size
     * depends on the host PC's NIC's MTU settings. To determine the frame size,
     * we test for support up to an expected "ceiling". If the user
     * specified a frame size, we use that frame size as the ceiling. If no
     * frame size was specified, we use the maximum UHD frame size.
     *
     * To optimize performance, the frame size should be greater than or equal
     * to the frame size that UHD uses so that frames don't get split across
     * multiple transmission units - this is why the limits passed into the
     * 'determine_max_frame_size' function are actually frame sizes. */
    frame_size_t req_max_frame_size;
    req_max_frame_size.recv_frame_size =
        (recv_args.has_key("recv_frame_size"))
            ? boost::lexical_cast<size_t>(recv_args["recv_frame_size"])
            : x300::DATA_FRAME_MAX_SIZE;
    req_max_frame_size.send_frame_size =
        (send_args.has_key("send_frame_size"))
            ? boost::lexical_cast<size_t>(send_args["send_frame_size"])
            : x300::DATA_FRAME_MAX_SIZE;

#if defined UHD_PLATFORM_LINUX
    const std::string mtu_tool("ip link");
#elif defined UHD_PLATFORM_WIN32
    const std::string mtu_tool("netsh");
#else
    const std::string mtu_tool("ifconfig");
#endif

    // Detect the frame size on the path to the USRP
    try {
        frame_size_t pri_frame_sizes =
            determine_max_frame_size(get_pri_eth().addr, req_max_frame_size);

        _max_frame_sizes = pri_frame_sizes;
        if (eth_conns.size() > 1) {
            frame_size_t sec_frame_sizes =
                determine_max_frame_size(eth_conns.at(1).addr, req_max_frame_size);

            // Choose the minimum of the max frame sizes
            // to ensure we don't exceed any one of the links' MTU
            _max_frame_sizes.recv_frame_size = std::min(
                pri_frame_sizes.recv_frame_size, sec_frame_sizes.recv_frame_size);

            _max_frame_sizes.send_frame_size = std::min(
                pri_frame_sizes.send_frame_size, sec_frame_sizes.send_frame_size);
        }
    } catch (std::exception& e) {
        UHD_LOGGER_ERROR("X300") << e.what();
    }

    if ((recv_args.has_key("recv_frame_size"))
        && (req_max_frame_size.recv_frame_size > _max_frame_sizes.recv_frame_size)) {
        UHD_LOGGER_WARNING("X300")
            << boost::format("You requested a receive frame size of (%lu) but your "
                             "NIC's max frame size is (%lu).")
                   % req_max_frame_size.recv_frame_size % _max_frame_sizes.recv_frame_size
            << boost::format("Please verify your NIC's MTU setting using '%s' or set "
                             "the recv_frame_size argument appropriately.")
                   % mtu_tool
            << "UHD will use the auto-detected max frame size for this connection.";
    }

    if ((send_args.has_key("send_frame_size"))
        && (req_max_frame_size.send_frame_size > _max_frame_sizes.send_frame_size)) {
        UHD_LOGGER_WARNING("X300")
            << boost::format("You requested a send frame size of (%lu) but your "
                             "NIC's max frame size is (%lu).")
                   % req_max_frame_size.send_frame_size % _max_frame_sizes.send_frame_size
            << boost::format("Please verify your NIC's MTU setting using '%s' or set "
                             "the send_frame_size argument appropriately.")
                   % mtu_tool
            << "UHD will use the auto-detected max frame size for this connection.";
    }

    // Check frame sizes
    for (auto conn : eth_conns) {
        link_max_rate += conn.link_rate;

        size_t rec_send_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                         ? GE_DATA_FRAME_SEND_SIZE
                                         : XGE_DATA_FRAME_SEND_SIZE;
        size_t rec_recv_frame_size = conn.link_rate == MAX_RATE_1GIGE
                                         ? GE_DATA_FRAME_RECV_SIZE
                                         : XGE_DATA_FRAME_RECV_SIZE;

        if (_max_frame_sizes.send_frame_size < rec_send_frame_size) {
            UHD_LOGGER_WARNING("X300")
                << boost::format("For the %s connection, UHD recommends a send frame "
                                 "size of at least %lu for best\nperformance, but "
                                 "your configuration will only allow %lu.")
                       % conn.addr % rec_send_frame_size
                       % _max_frame_sizes.send_frame_size
                << "This may negatively impact your maximum achievable sample "
                   "rate.\nCheck the MTU on the interface and/or the send_frame_size "
                   "argument.";
        }

        if (_max_frame_sizes.recv_frame_size < rec_recv_frame_size) {
            UHD_LOGGER_WARNING("X300")
                << boost::format("For the %s connection, UHD recommends a receive "
                                 "frame size of at least %lu for best\nperformance, "
                                 "but your configuration will only allow %lu.")
                       % conn.addr % rec_recv_frame_size
                       % _max_frame_sizes.recv_frame_size
                << "This may negatively impact your maximum achievable sample "
                   "rate.\nCheck the MTU on the interface and/or the recv_frame_size "
                   "argument.";
        }
    }

    _tree->create<size_t>("mtu/recv").set(_max_frame_sizes.recv_frame_size);
    _tree->create<size_t>("mtu/send").set(_max_frame_sizes.send_frame_size);
    _tree->access<double>("link_max_rate").set(link_max_rate);
}

size_t eth_manager::get_mtu(uhd::direction_t dir)
{
    return dir == uhd::RX_DIRECTION ? _max_frame_sizes.recv_frame_size
                                    : _max_frame_sizes.send_frame_size;
}


void eth_manager::discover_eth(
    const mboard_eeprom_t mb_eeprom, const std::string& loaded_fpga_image)
{
    udp_simple_factory_t udp_make_connected = x300_get_udp_factory(send_args);
    // Load all valid, non-duplicate IP addrs
    std::vector<std::string> ip_addrs{_args.get_first_addr()};
    if (not _args.get_second_addr().empty()
        && (_args.get_first_addr() != _args.get_second_addr())) {
        ip_addrs.push_back(_args.get_second_addr());
    }

    // Clear any previous addresses added
    eth_conns.clear();

    // Index the MB EEPROM addresses
    std::vector<std::string> mb_eeprom_addrs;
    const size_t num_mb_eeprom_addrs = 4;
    for (size_t i = 0; i < num_mb_eeprom_addrs; i++) {
        const std::string key = "ip-addr" + boost::to_string(i);

        // Show a warning if there exists duplicate addresses in the mboard eeprom
        if (std::find(mb_eeprom_addrs.begin(), mb_eeprom_addrs.end(), mb_eeprom[key])
            != mb_eeprom_addrs.end()) {
            UHD_LOGGER_WARNING("X300") << str(
                boost::format(
                    "Duplicate IP address %s found in mboard EEPROM. "
                    "Device may not function properly. View and reprogram the values "
                    "using the usrp_burn_mb_eeprom utility.")
                % mb_eeprom[key]);
        }
        mb_eeprom_addrs.push_back(mb_eeprom[key]);
    }

    for (const std::string& addr : ip_addrs) {
        x300_eth_conn_t conn_iface;
        conn_iface.addr = addr;
        conn_iface.type = X300_IFACE_NONE;

        // Decide from the mboard eeprom what IP corresponds
        // to an interface
        for (size_t i = 0; i < mb_eeprom_addrs.size(); i++) {
            if (addr == mb_eeprom_addrs[i]) {
                // Choose the interface based on the index parity
                if (i % 2 == 0) {
                    conn_iface.type      = X300_IFACE_ETH0;
                    conn_iface.link_rate = loaded_fpga_image == "HG" ? MAX_RATE_1GIGE
                                                                     : MAX_RATE_10GIGE;
                } else {
                    conn_iface.type      = X300_IFACE_ETH1;
                    conn_iface.link_rate = MAX_RATE_10GIGE;
                }
                break;
            }
        }

        // Check default IP addresses if we couldn't
        // determine the IP from the mboard eeprom
        if (conn_iface.type == X300_IFACE_NONE) {
            UHD_LOGGER_WARNING("X300") << str(
                boost::format(
                    "Address %s not found in mboard EEPROM. Address may be wrong or "
                    "the EEPROM may be corrupt. Attempting to continue with default "
                    "IP addresses.")
                % conn_iface.addr);

            if (addr
                == boost::asio::ip::address_v4(uint32_t(X300_DEFAULT_IP_ETH0_1G))
                       .to_string()) {
                conn_iface.type      = X300_IFACE_ETH0;
                conn_iface.link_rate = MAX_RATE_1GIGE;
            } else if (addr
                       == boost::asio::ip::address_v4(uint32_t(X300_DEFAULT_IP_ETH1_1G))
                              .to_string()) {
                conn_iface.type      = X300_IFACE_ETH1;
                conn_iface.link_rate = MAX_RATE_1GIGE;
            } else if (addr
                       == boost::asio::ip::address_v4(uint32_t(X300_DEFAULT_IP_ETH0_10G))
                              .to_string()) {
                conn_iface.type      = X300_IFACE_ETH0;
                conn_iface.link_rate = MAX_RATE_10GIGE;
            } else if (addr
                       == boost::asio::ip::address_v4(uint32_t(X300_DEFAULT_IP_ETH1_10G))
                              .to_string()) {
                conn_iface.type      = X300_IFACE_ETH1;
                conn_iface.link_rate = MAX_RATE_10GIGE;
            } else {
                throw uhd::assertion_error(
                    str(boost::format(
                            "X300 Initialization Error: Failed to match address %s with "
                            "any addresses for the device. Please check the address.")
                        % conn_iface.addr));
            }
        }

        // Save to a vector of connections
        if (conn_iface.type != X300_IFACE_NONE) {
            // Check the address before we add it
            try {
                wb_iface::sptr zpu_ctrl = x300_make_ctrl_iface_enet(
                    udp_make_connected(
                        conn_iface.addr, BOOST_STRINGIZE(X300_FW_COMMS_UDP_PORT)),
                    false /* Suppress timeout errors */
                );

                // Peek the ZPU ctrl to make sure this connection works
                zpu_ctrl->peek32(0);
            }

            // If the address does not work, throw an error
            catch (std::exception&) {
                throw uhd::io_error(
                    str(boost::format("X300 Initialization Error: Invalid address %s")
                        % conn_iface.addr));
            }
            eth_conns.push_back(conn_iface);
        }
    }

    if (eth_conns.size() == 0)
        throw uhd::assertion_error(
            "X300 Initialization Error: No ethernet interfaces specified.");
}

eth_manager::frame_size_t eth_manager::determine_max_frame_size(
    const std::string& addr, const frame_size_t& user_frame_size)
{
    auto udp = _x300_make_udp_connected(addr, BOOST_STRINGIZE(X300_MTU_DETECT_UDP_PORT));

    std::vector<uint8_t> buffer(
        std::max(user_frame_size.recv_frame_size, user_frame_size.send_frame_size));
    x300_mtu_t* request           = reinterpret_cast<x300_mtu_t*>(&buffer.front());
    constexpr double echo_timeout = 0.020; // 20 ms

    // test holler - check if its supported in this fw version
    request->flags = uhd::htonx<uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
    request->size  = uhd::htonx<uint32_t>(sizeof(x300_mtu_t));
    udp->send(boost::asio::buffer(buffer, sizeof(x300_mtu_t)));
    udp->recv(boost::asio::buffer(buffer), echo_timeout);
    if (!(uhd::ntohx<uint32_t>(request->flags) & X300_MTU_DETECT_ECHO_REPLY)) {
        throw uhd::not_implemented_error("Holler protocol not implemented");
    }

    // Reducing range of (min,max) by setting max value to 10gig max_frame_size as larger
    // sizes are not supported
    size_t min_recv_frame_size = sizeof(x300_mtu_t);
    size_t max_recv_frame_size =
        std::min(user_frame_size.recv_frame_size, x300::DATA_FRAME_MAX_SIZE) & size_t(~3);
    size_t min_send_frame_size = sizeof(x300_mtu_t);
    size_t max_send_frame_size =
        std::min(user_frame_size.send_frame_size, x300::DATA_FRAME_MAX_SIZE) & size_t(~3);

    UHD_LOGGER_DEBUG("X300") << "Determining maximum frame size... ";
    while (min_recv_frame_size < max_recv_frame_size) {
        size_t test_frame_size = (max_recv_frame_size / 2 + min_recv_frame_size / 2 + 3)
                                 & ~3;

        request->flags = uhd::htonx<uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
        request->size  = uhd::htonx<uint32_t>(test_frame_size);
        udp->send(boost::asio::buffer(buffer, sizeof(x300_mtu_t)));

        size_t len = udp->recv(boost::asio::buffer(buffer), echo_timeout);

        if (len >= test_frame_size)
            min_recv_frame_size = test_frame_size;
        else
            max_recv_frame_size = test_frame_size - 4;
    }

    if (min_recv_frame_size < IP_PROTOCOL_MIN_MTU_SIZE - IP_PROTOCOL_UDP_PLUS_IP_HEADER) {
        throw uhd::runtime_error("System receive MTU size is less than the minimum "
                                 "required by the IP protocol.");
    }

    while (min_send_frame_size < max_send_frame_size) {
        size_t test_frame_size = (max_send_frame_size / 2 + min_send_frame_size / 2 + 3)
                                 & ~3;

        request->flags = uhd::htonx<uint32_t>(X300_MTU_DETECT_ECHO_REQUEST);
        request->size  = uhd::htonx<uint32_t>(sizeof(x300_mtu_t));
        udp->send(boost::asio::buffer(buffer, test_frame_size));

        size_t len = udp->recv(boost::asio::buffer(buffer), echo_timeout);
        if (len >= sizeof(x300_mtu_t))
            len = uhd::ntohx<uint32_t>(request->size);

        if (len >= test_frame_size)
            min_send_frame_size = test_frame_size;
        else
            max_send_frame_size = test_frame_size - 4;
    }

    if (min_send_frame_size < IP_PROTOCOL_MIN_MTU_SIZE - IP_PROTOCOL_UDP_PLUS_IP_HEADER) {
        throw uhd::runtime_error(
            "System send MTU size is less than the minimum required by the IP protocol.");
    }

    frame_size_t frame_size;
    // There are cases when NICs accept oversized packets, in which case we'd falsely
    // detect a larger-than-possible frame size. A safe and sensible value is the minimum
    // of the recv and send frame sizes.
    frame_size.recv_frame_size = std::min(min_recv_frame_size, min_send_frame_size);
    frame_size.send_frame_size = std::min(min_recv_frame_size, min_send_frame_size);
    UHD_LOGGER_INFO("X300") << "Maximum frame size: " << frame_size.send_frame_size
                            << " bytes.";
    return frame_size;
}
