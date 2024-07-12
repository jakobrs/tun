#include <lean/lean.h>

#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" LEAN_EXPORT lean_obj_res sys_read(b_lean_obj_arg fd,
                                             lean_obj_arg buf) {
  if (!lean_is_exclusive(buf))
    buf = lean_copy_byte_array(buf);

  auto buf_sarray = lean_to_sarray(buf);

  auto sz =
      read(lean_scalar_to_int(fd), buf_sarray->m_data, buf_sarray->m_capacity);
  buf_sarray->m_size = sz;

  if (sz < 0) {
    return lean_io_result_mk_error(
        lean_mk_io_user_error(lean_mk_string("read failed")));
  }

  return lean_io_result_mk_ok(buf);
}

extern "C" LEAN_EXPORT lean_obj_res sys_write(b_lean_obj_arg fd,
                                              b_lean_obj_arg buf,
                                              lean_obj_arg) {
  auto buf_sarray = lean_to_sarray(buf);
  auto sz =
      write(lean_scalar_to_int(fd), buf_sarray->m_data, buf_sarray->m_size);

  if (sz < 0) {
    perror("write");
    return lean_io_result_mk_error(
        lean_mk_io_user_error(lean_mk_string("write failed")));
  }

  // TODO: handle case when not all is written
  assert((sz == buf_sarray->m_size));

  return lean_io_result_mk_ok(lean_box(0));
}

namespace {
struct Tun {
  std::string name;
  int fd;

  Tun(const char *name) {
    auto fd = open("/dev/net/tun", O_RDWR);

    if (fd < 0) {
      perror("opening /dev/net/tun failed");
      std::exit(1); // TODO: fix
    }

    ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (name)
      strncpy(ifr.ifr_name, name, IFNAMSIZ);

    auto err = ioctl(fd, TUNSETIFF, &ifr);
    if (err < 0) {
      perror("ioctl failed");
      std::exit(1); // TODO: fix
    }

    this->name = ifr.ifr_name;
    this->fd = fd;
  }

  ~Tun() {
    auto err = close(fd);
    if (err < 0) {
      perror("close failed");
      std::exit(1); // TODO: fix
    }
  }
};

void tun_finalize(void *dev) { delete static_cast<Tun *>(dev); }
void tun_foreach(void *, lean_obj_arg) {
  // do nothing
}

lean_external_class *tun_class = nullptr;
lean_external_class *get_tun_class() {
  if (!tun_class) [[unlikely]] {
    tun_class = lean_register_external_class(tun_finalize, tun_foreach);
  }

  return tun_class;
}

Tun &as_tun(b_lean_obj_arg tun) {
  return *static_cast<Tun *>(lean_get_external_data(tun));
}

struct Udp {
  int fd;

  Udp(int16_t port) {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      perror("creating UDP socket failed");
      std::exit(1);
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_addr.s_addr = INADDR_ANY;
    // inet_aton("127.0.0.1", &addr.sin_addr);
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    auto err = bind(fd, (sockaddr *)&addr, sizeof(addr));
    if (err < 0) {
      perror("bind failed");
      std::exit(1);
    }
  }

  ~Udp() { close(fd); }
};

void udp_finalize(void *udp) { delete static_cast<Udp *>(udp); }
void udp_foreach(void *, lean_obj_arg) {
  // do nothing
}

lean_external_class *udp_class = nullptr;
lean_external_class *get_udp_class() {
  if (!udp_class) [[unlikely]] {
    udp_class = lean_register_external_class(udp_finalize, udp_foreach);
  }

  return udp_class;
}

Udp &as_udp(b_lean_obj_arg obj) { return *(Udp *)lean_get_external_data(obj); }
} // namespace

extern "C" LEAN_EXPORT lean_obj_res
tun_mk(b_lean_obj_arg /* Option String */ name, lean_obj_arg) {
  Tun *dev;
  if (lean_obj_tag(name) == 0) {
    dev = new Tun(nullptr);
  } else {
    dev = new Tun(lean_string_cstr(lean_ctor_get(name, 0)));
  }

  auto cls = get_tun_class();
  auto obj = lean_alloc_external(cls, dev);
  return lean_io_result_mk_ok(obj);
}

extern "C" LEAN_EXPORT lean_obj_res tun_name(b_lean_obj_arg /* Tun */ tun) {
  return lean_mk_string(as_tun(tun).name.data());
}

extern "C" LEAN_EXPORT lean_obj_res tun_fd(b_lean_obj_arg /* Tun */ tun) {
  return lean_int_to_int(as_tun(tun).fd);
}

extern "C" LEAN_EXPORT lean_obj_res udp_mk(uint16_t port, lean_obj_arg) {
  auto socket = new Udp(port);
  auto cls = get_udp_class();
  auto obj = lean_alloc_external(cls, socket);
  return lean_io_result_mk_ok(obj);
}

extern "C" LEAN_EXPORT lean_obj_res udp_fd(b_lean_obj_arg /* Udp */ udp) {
  return lean_int_to_int(as_udp(udp).fd);
}

extern "C" LEAN_EXPORT lean_obj_res
udp_connect(b_lean_obj_arg /* Udp */ udp, b_lean_obj_arg /* String */ addr,
            uint16_t port, lean_obj_arg) {
  auto fd = as_udp(udp).fd;

  sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  inet_aton(lean_string_cstr(addr), &s_addr.sin_addr);
  s_addr.sin_port = htons(port);
  s_addr.sin_family = AF_INET;

  auto res = connect(fd, (sockaddr *)&s_addr, sizeof(s_addr));
  if (res < 0) {
    return lean_io_result_mk_error(
        lean_mk_io_user_error(lean_mk_string("Connect failed")));
  }

  return lean_io_result_mk_ok(lean_box(0));
}

extern "C" LEAN_EXPORT lean_obj_res
string_to_byte_array(b_lean_obj_arg /* String */ str) {
  auto data = lean_string_cstr(str);

  auto len = strlen(data);
  auto arr = lean_alloc_sarray(1, len, len);
  strcpy((char *)((lean_sarray_object *)(arr))->m_data, data);

  return arr;
}
