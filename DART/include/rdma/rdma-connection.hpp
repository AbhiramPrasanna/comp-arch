#pragma once

#include <cstdint>
#include <cstring>
#include <memory.h>
#include <inttypes.h>
#include <byteswap.h>

#include <infiniband/verbs.h>

#include "log/log.hpp"
#include "measure/measure.hpp"
#include "socket/socket.hpp"

#include "rdma/rdma-basic.hpp"


bool huge_page_alloc(uintptr_t& addr, uint64_t size);
bool huge_page_free(uintptr_t addr, uint64_t size);

namespace RDMA {

struct Usage {
    print_map read_size_times, read_fail_size_times;
    print_map write_size_times, write_fail_size_times;
    print_map cas_size_times, cas_fail_size_times;
    // ...
    void print() {
      log_purple << "read_size_times" << std::endl;
      read_size_times.print();
      log_purple << "read_fail_size_times" << std::endl;
      read_fail_size_times.print();
      log_purple << "write_size_times" << std::endl;
      write_size_times.print();
      log_purple << "write_fail_size_times" << std::endl;
      write_fail_size_times.print();
      log_purple << "cas_size_times" << std::endl;
      cas_size_times.print();
      log_purple << "cas_fail_size_times" << std::endl;
      cas_fail_size_times.print();
    }
};

// The RDMAMemoryRegion is used for generate an MR disregard of RDMAConnection.
// This can be used when you need different RDMAConnections with the same mr.

class RDMAMemoryRegion {

  public:
    RDMAMemoryRegion() = default;
    bool create(uint64_t mr_size_byte, uint8_t ib_dev, uint8_t ib_port, int gid_idx = -1);
    ~RDMAMemoryRegion();

    struct rdma_normal_data normal_data;
    struct ibv_mr* mr = nullptr;
    uint8_t ib_dev;
    uint8_t ib_port;
    int gid_idx;
};

class RDMAQueuePair {
  public:
    RDMAQueuePair() = default;
    bool create(RDMAMemoryRegion& rdma_mr);
    rdma_connect_data generate_connect_data() const;
    ~RDMAQueuePair();
    RDMAMemoryRegion* rdma_mr;
    struct rdma_connect_data connect_data;
    struct ibv_qp* qp = nullptr;
    struct ibv_cq* send_cq = nullptr;
    struct ibv_cq* recv_cq = nullptr;
};

bool tcp_send_rdma_connect_data(sockets::SocketConnection& sock, const uint32_t mem_id_num, const uint32_t compute_num, const uint32_t thread_num, const rdma_connect_data& send_connect_data);
bool tcp_receive_rdma_connect_data(sockets::SocketConnection& sock, uint32_t& mem_id_num, uint32_t& compute_num, uint32_t& thread_num, rdma_connect_data& result_connect_data);

// The RDMAConnection is only one-to-one, one <server QP> to one <client QP>.
// This depends on a SocketConnection to exchange data,
// so you must connect a SocketConnection before connect this.
// .-----.
// You can use many RDMAConnection pairs, by sharing one same SocketConnection

class RDMAConnection {

  public:

    RDMAConnection() = default;

    /// This is for normal rdma connection.
    bool connect(
        uint64_t mr_size_byte,
        rdma_connect_data& remote_connect_data,
        uint8_t ib_dev, uint8_t ib_port, int gid_idx = -1
    );

    /// This is used for creating connetions with specific MR (QP generated inner).
    /// RDMAMemoryRegion: please create `RDMAMemoryRegion` first
    bool connect(
        RDMAMemoryRegion& rdma_mr,
        rdma_connect_data& remote_connect_data
    );
  
    /// This is used for creating connetions with specific QP.
    /// RDMAMemoryRegion: please create `RDMAMemoryRegion` first
    bool connect(
        RDMAQueuePair& rdma_qp,
        rdma_connect_data& remote_connect_data
    );

    ~RDMAConnection();

    [[nodiscard]] uintptr_t get_remote_mr_addr() const noexcept;
    [[nodiscard]] uint64_t get_remote_mr_size() const noexcept;
    [[nodiscard]] uintptr_t get_local_mr_addr() const noexcept;
    [[nodiscard]] uint64_t get_local_mr_size() const noexcept;

    void read_from_local_mr_offset(uintptr_t dest, uintptr_t local_mr_offset, uint64_t size);
    void read_from_local_mr_addr(uintptr_t dest, uintptr_t local_mr_addr, uint64_t size);
    void write_to_local_mr_offset(uintptr_t local_mr_offset, uintptr_t src, uint64_t size);
    void write_to_local_mr_addr(uintptr_t local_mr_addr, uintptr_t src, uint64_t size);
  
    // client
    [[nodiscard]] bool read_from_remote_offset(uintptr_t local_mr_offset, uintptr_t remote_mr_offset, uint64_t size, bool poll = true);
    [[nodiscard]] bool read_from_remote_addr(uintptr_t local_mr_addr, uintptr_t remote_mr_addr, uint64_t size, bool poll = true);
    [[nodiscard]] bool write_to_remote_offset(uintptr_t remote_mr_offset, uintptr_t local_mr_offset, uint64_t size, bool poll = true);
    [[nodiscard]] bool write_to_remote_addr(uintptr_t remote_mr_addr, uintptr_t local_mr_addr, uint64_t size, bool poll = true);
    [[nodiscard]] bool cas_8_to_remote_offset(
        uintptr_t remote_mr_offset, uintptr_t local_save_old_mr_offset,
        uint64_t swap_data, uint64_t compare_data
    );
    [[nodiscard]] bool cas_8_to_remote_addr(
        uintptr_t remote_mr_addr, uintptr_t local_save_old_mr_addr,
        uint64_t swap_data, uint64_t compare_data
    );
    [[nodiscard]] bool poll_cq(uint32_t num);

    void print_usage() {
      usage.print();
    }

    void reset_usage() {
      usage = Usage();
    }

    Usage usage;

  private:
    [[nodiscard]] bool modify_QP();
    bool inner_create_new_qp = false;
    bool inner_create_new_mr = false;
    struct rdma_connect_data remote_connect_data;
    struct RDMAQueuePair* rdma_qp;
    struct RDMAMemoryRegion* rdma_mr;

};


class RDMAConnectionMetadata {
  private:
    rdma_connect_data* connection_data;
    uint32_t total_memory_num, compute_machine_num, thread_num;
  public:
    [[nodiscard]] rdma_connect_data& operator()(uint32_t memory_ind, uint32_t compute_ind, uint32_t thread_ind) {
        return connection_data[memory_ind + (compute_ind + thread_ind * compute_machine_num) * total_memory_num];
    }
    RDMAConnectionMetadata(uint32_t total_memory_num, uint32_t compute_machine_num, uint32_t thread_num):
      total_memory_num(total_memory_num), compute_machine_num(compute_machine_num), thread_num(thread_num) {
        connection_data = new rdma_connect_data[total_memory_num * compute_machine_num * thread_num];
    }
    ~RDMAConnectionMetadata() {
        delete[] connection_data;
    }
};

}


namespace DM {

constexpr uintptr_t MEM_PTR_MASK = 0x0000'0fff'ffff'ffffull;
constexpr uintptr_t MEM_ID_MASK = 0x0000'f000'0000'0000ull;
constexpr uint32_t MEM_ID_SHIFT = 44;
constexpr uint32_t MAX_MEMORY_NUM = 16;

// for one thread
class DisaggregatedMemoryController {

  private:

    // for one thread connect to all memory machines
    RDMA::RDMAConnection* connections;
    uint32_t total_memory_num = 0;
    uint32_t total_compute_num = 0;
    uint32_t total_thread_num = 0;
    uint32_t compute_index = 0;
    uint32_t thread_index = 0;

    uint32_t alloc_mem_id;
    uintptr_t alloc_start_offset;
    uintptr_t alloc_end_offset;
    uint64_t alloc_size;

  public:

    [[nodiscard]] RDMA::RDMAConnection& operator()(uint32_t memory_ind) {
        return connections[memory_ind];
    }

    DisaggregatedMemoryController(
      RDMA::RDMAConnection* connections, uint32_t total_memory_num, uint32_t total_compute_num, uint32_t total_thread_num,
      uint32_t compute_index, uint32_t thread_index
    ):
      total_memory_num(total_memory_num), total_compute_num(total_compute_num), total_thread_num(total_thread_num),
      compute_index(compute_index), thread_index(thread_index)
    {
        this->connections = new RDMA::RDMAConnection[total_memory_num];
        for (uint32_t i = 0; i < total_memory_num; ++i) {
            this->connections[i] = connections[i];
        }

        // count index
        auto end_ind = total_compute_num * total_thread_num;
        auto this_ind = compute_index * total_thread_num + thread_index;
        auto thread_alloc_per_mem = (end_ind + total_memory_num - 1) / total_memory_num;

        // count allocate size
        alloc_mem_id = this_ind / thread_alloc_per_mem;
        auto alloc_mem_total_memory = (*this)(alloc_mem_id).get_remote_mr_size();
        alloc_size = alloc_mem_total_memory / thread_alloc_per_mem;
        alloc_size = (alloc_size + 4_KiB - 1) / 4_KiB * 4_KiB;

        // count allocate offset
        alloc_start_offset = (this_ind % thread_alloc_per_mem) * alloc_size;
        if (alloc_start_offset > alloc_mem_total_memory)
          alloc_start_offset = alloc_mem_total_memory;
        alloc_end_offset = alloc_start_offset + alloc_size;
        if (alloc_end_offset > alloc_mem_total_memory)
          alloc_end_offset = alloc_mem_total_memory;
        if (alloc_end_offset <= alloc_start_offset)
          log_error << "thread " << thread_index + 1 << "/" << total_thread_num << " alloc_end_offset <= alloc_start_offset" << std::endl;

        // determine allocate size
        alloc_size = alloc_end_offset - alloc_start_offset;
    };

    ~DisaggregatedMemoryController() {
        delete[] this->connections;
    }

    [[nodiscard]] bool check_local_memory() {
        const auto start = this->connections[0].get_local_mr_addr();
        const auto size = this->connections[0].get_local_mr_size();
        for (uint32_t i = 0; i < total_memory_num; ++i) {
            if (this->connections[i].get_local_mr_addr() != start || this->connections[i].get_local_mr_size() != size) {
                log_error << "Local memory is not the same!" << std::endl;
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] uintptr_t get_root_start_fptr() {
        return offset2fptr(0, 0);
    }

    [[nodiscard]] uintptr_t get_alloc_mem_id() {
        return alloc_mem_id;
    }

    [[nodiscard]] uintptr_t get_alloc_start_fptr() {
        return offset2fptr(alloc_mem_id, alloc_start_offset);
    }

    [[nodiscard]] uintptr_t get_alloc_end_fptr() {
        return offset2fptr(alloc_mem_id, alloc_end_offset);
    }

    [[nodiscard]] uintptr_t get_local_start_ptr() {
        // same for [1] [2] ...
        return this->connections[0].get_local_mr_addr();
    }

    [[nodiscard]] uintptr_t get_local_end_ptr() {
        // same for [1] [2] ...
        return this->connections[0].get_local_mr_addr() + this->connections[0].get_local_mr_size();
    }

    [[nodiscard]] uintptr_t get_local_size() {
        // same for [1] [2] ...
        return this->connections[0].get_local_mr_size();
    }
  
    [[nodiscard]] uintptr_t get_alloc_size() {
        return alloc_size;
    }

    bool poll_cq_from_memid(uint32_t mem_id) {
        return (*this)(mem_id).poll_cq(1);
    }

    bool poll_cq_from_fptr(uintptr_t fptr) {
        uint32_t mem_id_num = fptr2memid(fptr);
        return (*this)(mem_id_num).poll_cq(1);
    }

    // dest, source, size
    bool read_from_remote_fptr_to_local_offset(
        uintptr_t local_mr_offset, uintptr_t fptr, uint64_t size, bool poll
    ) {
        return (*this)(fptr2memid(fptr)).read_from_remote_offset(
            local_mr_offset,
            fptr2offset(fptr),
            size,
            poll
        );
    }

    // dest, source, size
    bool read_from_remote_fptr_to_local_addr(
        uintptr_t local_mr_addr, uintptr_t fptr, uint64_t size, bool poll
    ) {
        return (*this)(fptr2memid(fptr)).read_from_remote_addr(
            local_mr_addr,
            fptr2realptr(fptr),
            size,
            poll
        );
    }

    // dest, source, size
    bool write_from_local_offset_to_remote_fptr(
        uintptr_t fptr, uintptr_t local_mr_offset, uint64_t size, bool poll
    ) {
        return (*this)(fptr2memid(fptr)).write_to_remote_offset(
            fptr2offset(fptr),
            local_mr_offset,
            size,
            poll
        );
    }

    // dest, source, size
    bool write_from_local_addr_to_remote_fptr(
        uintptr_t fptr,uintptr_t local_mr_addr, uint64_t size, bool poll
    ) {
        return (*this)(fptr2memid(fptr)).write_to_remote_addr(
            fptr2realptr(fptr),
            local_mr_addr,
            size,
            poll
        );
    }

    bool cas_8_byte_to_remote_fptr(
        uintptr_t fptr, uintptr_t local_save_old_mr_addr, uint64_t swap_data, uint64_t compare_data
    ) {
        return (*this)(fptr2memid(fptr)).cas_8_to_remote_addr(
            fptr2realptr(fptr),
            local_save_old_mr_addr,
            swap_data,
            compare_data
        );
    }
    // realptr + mem_id to fptr:
    //   64             48                 44                                                    0
    //   [ high 16 bit ][  mem_id (4 bit) ][ start from base_offset (realptr - remote_base_addr) ]

    [[nodiscard]] uint32_t fptr2memid(uintptr_t fake_pointer) {
        return (fake_pointer & MEM_ID_MASK) >> MEM_ID_SHIFT;
    }

    [[nodiscard]] uintptr_t fptr2realptr(uintptr_t fake_pointer) {
        uint32_t mem_id_num = (fake_pointer & MEM_ID_MASK) >> MEM_ID_SHIFT;
        return signed48to64((fake_pointer & MEM_PTR_MASK) + (*this)(mem_id_num).get_remote_mr_addr());
    }

    [[nodiscard]] uintptr_t fptr2offset(uintptr_t fake_pointer) {
        return signed48to64((fake_pointer & MEM_PTR_MASK));
    }

    // fptr to readptr + mem_id
    [[nodiscard]] uintptr_t fptr2realptr_memid(uint32_t& output_mem_id, uintptr_t fake_pointer) {
        uint32_t mem_id_num = (fake_pointer & MEM_ID_MASK) >> MEM_ID_SHIFT;
        uintptr_t real_pointer = signed48to64((fake_pointer & MEM_PTR_MASK) + (*this)(mem_id_num).get_remote_mr_addr());
        output_mem_id = mem_id_num;
        return real_pointer;
    }
    // fptr to offset + mem_id
    [[nodiscard]] uintptr_t fptr2offset_memid(uint32_t& output_mem_id, uintptr_t fake_pointer) {
        uint32_t mem_id_num = (fake_pointer & MEM_ID_MASK) >> MEM_ID_SHIFT;
        uintptr_t offset = signed48to64((fake_pointer & MEM_PTR_MASK));
        output_mem_id = mem_id_num;
        return offset;
    }

    [[nodiscard]] uintptr_t offset2fptr(uint32_t mem_id_num, uintptr_t pointer_offset) {
        if ((pointer_offset & MEM_ID_MASK) != 0) {
            log_error << "Pointer is out of range! offset = " << hex_str(pointer_offset) << std::endl;
            return 0;
        }
        uintptr_t fake_pointer = pointer_offset | ((uintptr_t)mem_id_num << MEM_ID_SHIFT);
        return fake_pointer;
    }

    [[nodiscard]] uintptr_t realptr2fptr(uint32_t mem_id_num, uintptr_t real_pointer) {
        uintptr_t fake_pointer = real_pointer & pointer_48bit_mask - (*this)(mem_id_num).get_remote_mr_addr();
        if ((fake_pointer & MEM_ID_MASK) != 0) {
            log_error << "Pointer is out of range! pointer = " << hex_str(real_pointer) << ", offset = " << hex_str(fake_pointer) << std::endl;
            return 0;
        }
        fake_pointer = fake_pointer | ((uintptr_t)mem_id_num << MEM_ID_SHIFT);
        return fake_pointer;
    }

};

}