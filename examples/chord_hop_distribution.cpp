#include "chord.hpp"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " b n m" << std::endl;
        return 1;
    }
    uint64_t bits = atoi(argv[1]);
    uint64_t nodes = atoi(argv[2]);
    uint64_t messages = atoi(argv[3]);
    std::vector<uint64_t> counts(bits+1);
    std::atomic<uint64_t> received_messages{0};
    auto complete_callback = [&](const Node<std::size_t>* n, Message<std::size_t> msg) {
        counts[msg.get_hops()]++;
        received_messages++;
    };
    HardwareManager<std::size_t> hwm(1<<bits, 4);
    for (unsigned i=0; i<nodes; i++) {
        hwm.add_node<ChordNode>(hwm.gen_id(), bits, complete_callback);
        //std::cerr << "Added node " << i << std::endl;
    }
    hwm.run();
    for (unsigned i=0; i<messages; i++) {
        hwm.gen_message(hwm.get_random_node());
        //std::cerr << "Generated message " << i << std::endl;
    }
    while (received_messages != messages) {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(10ms);
    }
    hwm.stop();
    std::cout.precision(3);
    for (unsigned i=1; i<bits+1; i++) {
        std::cout << 1.0*counts[i]/received_messages << " ";
    }
    std::cout << std::endl;
}
