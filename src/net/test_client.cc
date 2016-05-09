#include <mpc/lsic.hh>
#include <mpc/private_comparison.hh>
#include <mpc/rev_enc_comparison.hh>
#include <mpc/garbled_comparison.hh>
#include <mpc/enc_comparison.hh>
#include <mpc/linear_enc_argmax.hh>
#include <mpc/tree_enc_argmax.hh>

#include <net/client.hh>
#include <net/protocol_tester.hh>

#include <util/util.hh>

#include <util/benchmarks.hh>

static void test_basic_client(const string &hostname)
{

#ifdef BENCHMARK
    cout << "BENCHMARK flag set" << endl;
    BENCHMARK_INIT
#endif

    try
    {
        
        boost::asio::io_service io_service;
        
        gmp_randstate_t randstate;
        gmp_randinit_default(randstate);
        gmp_randseed_ui(randstate,time(NULL));
        
        Tester_Client client(io_service, randstate,1024,100);
        
        client.connect(io_service, hostname);
        
        client.exchange_keys();
//        client.get_server_pk_fhe();
        
        // server has b = 20
        
        ScopedTimer *t_lsic = new ScopedTimer("LSIC");
        mpz_class res_lsic = client.test_lsic(40,100);
        delete t_lsic;
//        client.test_decrypt_gm(res_lsic);

        ScopedTimer *t_comp = new ScopedTimer("Comp");
        mpz_class res_comp = client.test_compare(40,100);
        delete t_comp;
        
        ScopedTimer *t_garbled_comp = new ScopedTimer("Garbled Comp");
        mpz_class res_garbled_comp = client.test_garbled_compare(40,100);
        delete t_garbled_comp;

//        client.test_rev_enc_compare(64);
//        client.test_enc_compare(64);
//        client.test_linear_enc_argmax();
//        client.test_tree_enc_argmax();
//        client.test_fhe();
//        client.test_change_es();
        
//        client.test_multiple_enc_compare(40);
        
//        client.test_ot(15);
        
        client.disconnect();
        
    }
    catch (std::exception& e)
    {
        std::cout << "Exception: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: client <host>" << std::endl;
        return 1;
    }
    string hostname(argv[1]);

    test_basic_client(hostname);
    
    return 0;
}
