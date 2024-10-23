
namespace smartspeaker {

  template <typename E>
  class EnumIterator {
    using int_type = typename std::underlying_type<E>::type;
    int_type value;

  public:
    explicit EnumIterator(const E& f) : value(static_cast<int_type>(f)) {}
    void operator++() { ++value; }
    E operator*() const { return static_cast<E>(value); }
    bool operator!=(const EnumIterator& other) const { return value != other.value; }

    static EnumIterator begin() { return EnumIterator(static_cast<E>(0)); }
    static EnumIterator end() { return EnumIterator(E::Last); }
  };
}
