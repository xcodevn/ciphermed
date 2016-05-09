#include <iostream>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <gmpxx.h>
#include <thread>

#include <FHE.h>
#include <EncryptedArray.h>
#include <util/fhe_util.hh>

#include <net/defs.hh>

#include <crypto/paillier.hh>
#include <mpc/lsic.hh>
#include <mpc/private_comparison.hh>
#include <mpc/garbled_comparison.hh>
#include <mpc/enc_comparison.hh>
#include <mpc/rev_enc_comparison.hh>
#include <mpc/linear_enc_argmax.hh>
#include <mpc/tree_enc_argmax.hh>

#include <net/server.hh>
#include <net/net_utils.hh>
#include <net/message_io.hh>
#include <net/oblivious_transfer.hh>

#include <net/exec_protocol.hh>

#include <protobuf/protobuf_conversion.hh>

using boost::asio::ip::tcp;

using namespace std;

#define OT_SECPARAM 1024

Server::Server(gmp_randstate_t state, Key_dependencies_descriptor key_deps_desc, unsigned int keysize, unsigned int lambda)
: key_deps_desc_(key_deps_desc), paillier_(NULL), gm_(NULL), fhe_context_(NULL), fhe_sk_(NULL), n_clients_(0), threads_per_session_(1), lambda_(lambda)
{
    gmp_randinit_set(rand_state_, state);

    init_needed_keys(keysize);
    ObliviousTransfer::init(OT_SECPARAM);
}

Server::~Server()
{
    delete fhe_sk_;
    delete fhe_context_;
}

void Server::init_needed_keys(unsigned int keysize)
{
    if (key_deps_desc_.need_server_gm) {
        init_GM(keysize);
    }
    if (key_deps_desc_.need_server_paillier) {
        init_Paillier(keysize);
    }
    if (key_deps_desc_.need_server_fhe) {
        init_FHE_context();
        init_FHE_key();
    }
    
    
    if (key_deps_desc_.need_client_fhe) {
        init_FHE_context();
    }

}

void Server::init_GM(unsigned int keysize)
{
    if (gm_ != NULL) {
        return;
    }
    gm_ = new GM_priv(GM_priv::keygen(rand_state_,keysize),rand_state_);
}

void Server::init_Paillier(unsigned int keysize)
{
    if (paillier_ != NULL) {
        return;
    }
    
    paillier_ = new Paillier_priv_fast(Paillier_priv_fast::keygen(rand_state_,keysize), rand_state_);
        
}

void Server::init_FHE_context()
{
    if (fhe_context_) {
        return;
    }
    // generate a context. This one should be consisten with the server's one
    // i.e. m, p, r must be the same
    
    fhe_context_ = create_FHEContext(FHE_p,FHE_r,FHE_d,FHE_c,FHE_L,FHE_s,FHE_k,FHE_m);
    // we suppose d > 0
    fhe_G_ = makeIrredPoly(FHE_p, FHE_d);
}
void Server::init_FHE_key()
{
    if (fhe_sk_) {
        return;
    }

    fhe_sk_ = new FHESecKey(*fhe_context_);
    fhe_sk_->GenSecKey(FHE_w); // A Hamming-weight-w secret key
}

void Server::run()
{
    try
    {
        boost::asio::io_service io_service;
        
        tcp::endpoint endpoint(tcp::v4(), PORT);
        tcp::acceptor acceptor(io_service, endpoint);
        
        for (;;)
        {
            tcp::socket socket(io_service);
            acceptor.accept(socket);
            
            Server_session *c = create_new_server_session(socket);
            
            cout << "Start new connexion: " << c->id() << endl;
            c->run_session();
            // thread t (&Server_session::run_session,c);
            // t.detach();
        }
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
}


Server_session::Server_session(Server *server, gmp_randstate_t state, unsigned int id, tcp::socket &socket)
: server_(server), socket_(std::move(socket)), client_gm_(NULL), client_paillier_(NULL), client_fhe_pk_(NULL), id_(id)
{
    gmp_randinit_set(rand_state_, state);
}

Server_session::~Server_session()
{
    if (client_gm_) {
        delete client_gm_;
    }
}

void Server_session::send_paillier_pk()
{
    boost::asio::streambuf buff;
    std::ostream buff_stream(&buff);
    
    cout << id_ << ": Send Paillier PK" << endl;
    Protobuf::Paillier_PK pk_message = get_pk_message(&(server_->paillier()));
    
    sendMessageToSocket<Protobuf::Paillier_PK>(socket_,pk_message);
}

void Server_session::send_gm_pk()
{
    boost::asio::streambuf buff;
    std::ostream buff_stream(&buff);
    
    cout << id_ << ": Send GM PK" << endl;
    Protobuf::GM_PK pk_message = get_pk_message(&(server_->gm()));
    
    sendMessageToSocket<Protobuf::GM_PK>(socket_,pk_message);
}

void Server_session::send_fhe_context()
{
    const FHEcontext &context = server_->fhe_context();
    cout << id_ << ": Send FHE Context" << endl;
    Protobuf::FHE_Context pk_message = convert_to_message(context);
    
    sendMessageToSocket<Protobuf::FHE_Context>(socket_,pk_message);
}

void Server_session::send_fhe_pk()
{
    const FHEPubKey& publicKey = server_->fhe_sk(); // cast so we only send the public informations
    cout << id_ << ": Send FHE PK" << endl;

    Protobuf::FHE_PK pk_message = get_pk_message(publicKey);
    
    sendMessageToSocket<Protobuf::FHE_PK>(socket_,pk_message);

}

void Server_session::get_client_pk_gm()
{
    if (client_gm_) {
        return;
    }

    Protobuf::GM_PK pk = readMessageFromSocket<Protobuf::GM_PK>(socket_);
    cout << id_ << ": Received GM PK" << endl;
    client_gm_ = create_from_pk_message(pk,rand_state_);
}

void Server_session::get_client_pk_paillier()
{
    if (client_paillier_) {
        return;
    }

    Protobuf::Paillier_PK pk = readMessageFromSocket<Protobuf::Paillier_PK>(socket_);
    cout << id_ << ": Received Paillier PK" << endl;
    client_paillier_ = create_from_pk_message(pk,rand_state_);
}


void Server_session::get_client_pk_fhe()
{
    if (client_fhe_pk_) {
        return;
    }
    
    Protobuf::FHE_PK pk = readMessageFromSocket<Protobuf::FHE_PK>(socket_);
    cout << id_ << ": Received FHE PK" << endl;
    client_fhe_pk_ = create_from_pk_message(pk,server_->fhe_context());
}


void Server_session::exchange_keys()
{
    Key_dependencies_descriptor key_deps_desc = server_->key_deps_desc();
    if (key_deps_desc.need_server_gm) {
        send_gm_pk();
    }
    if (key_deps_desc.need_server_paillier) {
        send_paillier_pk();
    }

    if (key_deps_desc.need_client_gm) {
        get_client_pk_gm();
    }
    if (key_deps_desc.need_client_paillier) {
        get_client_pk_paillier();
    }
    
    if (key_deps_desc.need_server_fhe ||
        key_deps_desc.need_client_fhe) {
        // if we use FHE, we need to send the context to the client before doing anything
        send_fhe_context();
    }

    if (key_deps_desc.need_server_fhe) {
        send_fhe_pk();
    }
    if (key_deps_desc.need_client_fhe) {
        get_client_pk_fhe();
    }

}


mpz_class Server_session::run_comparison_protocol_A(Comparison_protocol_A *comparator)
{
    exec_comparison_protocol_A(socket_,comparator,server_->threads_per_session());
    return comparator->output();
}

mpz_class Server_session::run_lsic_A(LSIC_A *lsic)
{
    exec_lsic_A(socket_,lsic);
    return lsic->output();
}
mpz_class Server_session::run_priv_compare_A(Compare_A *comparator)
{
    exec_priv_compare_A(socket_,comparator,server_->threads_per_session());
    return comparator->output();
}

mpz_class Server_session::run_garbled_compare_A(GC_Compare_A *comparator)
{
    exec_garbled_compare_A(socket_,comparator);
    return comparator->output();
}

void Server_session::run_comparison_protocol_B(Comparison_protocol_B *comparator)
{
    exec_comparison_protocol_B(socket_,comparator,server_->threads_per_session());
}

void Server_session::run_lsic_B(LSIC_B *lsic)
{
    exec_lsic_B(socket_,lsic);
}

void Server_session::run_priv_compare_B(Compare_B *comparator)
{
    exec_priv_compare_B(socket_,comparator,server_->threads_per_session());
}

void Server_session::run_garbled_compare_B(GC_Compare_B *comparator)
{
    exec_garbled_compare_B(socket_,comparator);
}

// we suppose that the client already has the server's public key for Paillier
void Server_session::rev_enc_comparison(const mpz_class &a, const mpz_class &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    Rev_EncCompare_Owner owner = create_rev_enc_comparator_owner(l, comparison_prot);
    owner.set_input(a,b);
    run_rev_enc_comparison_owner(owner);
}

void Server_session::run_rev_enc_comparison_owner(Rev_EncCompare_Owner &owner)
{
    exec_rev_enc_comparison_owner(socket_, owner, server_->lambda(), true, server_->threads_per_session());
}

bool Server_session::help_rev_enc_comparison(const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    Rev_EncCompare_Helper helper = create_rev_enc_comparator_helper(l, comparison_prot);
    return run_rev_enc_comparison_helper(helper);
}

bool Server_session::run_rev_enc_comparison_helper(Rev_EncCompare_Helper &helper)
{
    exec_rev_enc_comparison_helper(socket_, helper, true, server_->threads_per_session());
    return helper.output();
}

bool Server_session::enc_comparison(const mpz_class &a, const mpz_class &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    EncCompare_Owner owner = create_enc_comparator_owner(l, comparison_prot);
    owner.set_input(a,b);
    
    return run_enc_comparison_owner(owner);
}

bool Server_session::run_enc_comparison_owner(EncCompare_Owner &owner)
{
    exec_enc_comparison_owner(socket_, owner, server_->lambda(), true, server_->threads_per_session());
    return owner.output();
}

void Server_session::help_enc_comparison(const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    EncCompare_Helper helper = create_enc_comparator_helper(l, comparison_prot);
    run_enc_comparison_helper(helper);
}

void Server_session::run_enc_comparison_helper(EncCompare_Helper &helper)
{
    exec_enc_comparison_helper(socket_,helper, true, server_->threads_per_session());
}


// same as before, but the result is encrypted under QR
// recall that in this case, the prefix 'rev_' has to be flipped

mpz_class Server_session::run_rev_enc_comparison_owner_enc_result(Rev_EncCompare_Owner &owner)
{
    exec_rev_enc_comparison_owner(socket_, owner, server_->lambda(), false, server_->threads_per_session());
    return owner.encrypted_output();
}

void Server_session::run_rev_enc_comparison_helper_enc_result(Rev_EncCompare_Helper &helper)
{
    exec_rev_enc_comparison_helper(socket_, helper, false, server_->threads_per_session());
}

void Server_session::run_enc_comparison_owner_enc_result(EncCompare_Owner &owner)
{
    exec_enc_comparison_owner(socket_, owner, server_->lambda(), false, server_->threads_per_session());
}

mpz_class Server_session::run_enc_comparison_helper_enc_result(EncCompare_Helper &helper)
{
    exec_enc_comparison_helper(socket_,helper, false, server_->threads_per_session());
    return helper.encrypted_output();
}

// here we keep the old convention as these are the function meant to be called

void Server_session::rev_enc_comparison_enc_result(const mpz_class &a, const mpz_class &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    EncCompare_Owner owner = create_enc_comparator_owner(l, comparison_prot);
    owner.set_input(a,b);
    run_enc_comparison_owner_enc_result(owner);
}


mpz_class Server_session::help_rev_enc_comparison_enc_result(const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    EncCompare_Helper helper = create_enc_comparator_helper(l, comparison_prot);
    return run_enc_comparison_helper_enc_result(helper);
}


mpz_class Server_session::enc_comparison_enc_result(const mpz_class &a, const mpz_class &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    Rev_EncCompare_Owner owner = create_rev_enc_comparator_owner(l, comparison_prot);
    owner.set_input(a,b);
    
    return run_rev_enc_comparison_owner_enc_result(owner);
}


void Server_session::help_enc_comparison_enc_result(const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    Rev_EncCompare_Helper helper = create_rev_enc_comparator_helper(l, comparison_prot);
    run_rev_enc_comparison_helper_enc_result(helper);
}

vector<bool> Server_session::multiple_enc_comparison(const vector<mpz_class> &a, const vector<mpz_class> &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    assert(a.size() == b.size());
    size_t n = a.size();
    vector<EncCompare_Owner*> owners(n);
    
    for (size_t i = 0; i < n; i++) {
        owners[i] = new EncCompare_Owner(create_enc_comparator_owner(l, comparison_prot));
        owners[i]->set_input(a[i],b[i]);
    }
    
    unsigned int thread_per_job = ceilf(((float)server_->threads_per_session())/n);
    multiple_exec_enc_comparison_owner(socket_, owners, server_->lambda(), true, thread_per_job);
    
    vector<bool> results(n);
    
    for (size_t i = 0; i < n; i++) {
        results[i] = owners[i]->output();
        delete owners[i];
    }
    
    return results;
}


void Server_session::multiple_help_enc_comparison(const size_t n, const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    vector<EncCompare_Helper*> helpers(n);
    
    for (size_t i = 0; i < n; i++) {
        helpers[i] = new EncCompare_Helper(create_enc_comparator_helper(l, comparison_prot));
        
    }
    
    unsigned int thread_per_job = ceilf(((float)server_->threads_per_session())/n);
    multiple_exec_enc_comparison_helper(socket_, helpers, true, thread_per_job);
    
    for (size_t i = 0; i < n; i++) {
        delete helpers[i];
    }
}


void Server_session::multiple_rev_enc_comparison(const vector<mpz_class> &a, const vector<mpz_class> &b, size_t l, COMPARISON_PROTOCOL comparison_prot)
{
    assert(a.size() == b.size());
    size_t n = a.size();
    vector<Rev_EncCompare_Owner*> owners(n);
    
    for (size_t i = 0; i < n; i++) {
        owners[i] = new Rev_EncCompare_Owner(create_rev_enc_comparator_owner(l, comparison_prot));
        owners[i]->set_input(a[i],b[i]);
    }
    
    unsigned int thread_per_job = ceilf(((float)server_->threads_per_session())/n);
    multiple_exec_rev_enc_comparison_owner(socket_, owners, server_->lambda(), true, thread_per_job);
    
    
    for (size_t i = 0; i < n; i++) {
        delete owners[i];
    }
    
}


vector<bool> Server_session::multiple_help_rev_enc_comparison(const size_t n, const size_t &l, COMPARISON_PROTOCOL comparison_prot)
{
    vector<Rev_EncCompare_Helper*> helpers(n);
    
    for (size_t i = 0; i < n; i++) {
        helpers[i] = new Rev_EncCompare_Helper(create_rev_enc_comparator_helper(l, comparison_prot));
        
    }
    
    unsigned int thread_per_job = ceilf(((float)server_->threads_per_session())/n);
    multiple_exec_rev_enc_comparison_helper(socket_, helpers, true, thread_per_job);
    
    vector<bool> results(n);
    for (size_t i = 0; i < n; i++) {
        results[i] = helpers[i]->output();
        delete helpers[i];
    }

    return results;
}

void Server_session::run_linear_enc_argmax(Linear_EncArgmax_Helper &helper, COMPARISON_PROTOCOL comparison_prot)
{
    size_t nbits = helper.bit_length();
    function<Comparison_protocol_B*()> comparator_creator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator_creator = [this,nbits](){ return new LSIC_B(0,nbits,server_->gm()); };
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator_creator = [this,nbits](){ return new Compare_B(0,nbits,server_->paillier(),server_->gm()); };
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator_creator = [this,nbits](){ return new GC_Compare_B(0,nbits,server_->gm(), rand_state_); };
    }
    exec_linear_enc_argmax(socket_, helper, comparator_creator, server_->threads_per_session());
}

void Server_session::run_tree_enc_argmax(Tree_EncArgmax_Helper &helper, COMPARISON_PROTOCOL comparison_prot)
{
    size_t nbits = helper.bit_length();
    function<Comparison_protocol_B*()> comparator_creator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator_creator = [this,nbits](){ return new LSIC_B(0,nbits,server_->gm()); };
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator_creator = [this,nbits](){ return new Compare_B(0,nbits,server_->paillier(),server_->gm()); };
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator_creator = [this,nbits](){ return new GC_Compare_B(0,nbits,server_->gm(), rand_state_); };
    }
    exec_tree_enc_argmax(socket_, helper, comparator_creator, server_->threads_per_session());
}

Ctxt Server_session::change_encryption_scheme(const vector<mpz_class> &c_gm)
{
    EncryptedArray ea(server_->fhe_context(), server_->fhe_G());
    
    return exec_change_encryption_scheme_slots(socket_, c_gm, *client_gm_ ,*client_fhe_pk_, ea, rand_state_);
}


void Server_session::run_change_encryption_scheme_slots_helper()
{
    EncryptedArray ea(server_->fhe_context(), server_->fhe_G());
    exec_change_encryption_scheme_slots_helper(socket_, server_->gm(), server_->fhe_sk(), ea);
}


mpz_class Server_session::compute_dot_product(const vector<mpz_class> &x)
{
    return exec_compute_dot_product(socket_, x, *client_paillier_);
}

void Server_session::help_compute_dot_product(const vector<mpz_class> &y, bool encrypted_input)
{
    exec_help_compute_dot_product(socket_, y, server_->paillier(), encrypted_input);
}

EncCompare_Owner Server_session::create_enc_comparator_owner(size_t bit_size, COMPARISON_PROTOCOL comparison_prot)
{
    Comparison_protocol_B *comparator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator = new LSIC_B(0,bit_size,server_->gm());
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator = new Compare_B(0,bit_size,server_->paillier(),server_->gm());
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator = new GC_Compare_B(0,bit_size,server_->gm(), rand_state_);
    }

    return EncCompare_Owner(0,0,bit_size,*client_paillier_,comparator,rand_state_);
}

EncCompare_Helper Server_session::create_enc_comparator_helper(size_t bit_size, COMPARISON_PROTOCOL comparison_prot)
{

    Comparison_protocol_A *comparator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator = new LSIC_A(0,bit_size,*client_gm_);
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator = new Compare_A(0,bit_size,*client_paillier_,*client_gm_,rand_state_);
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator = new GC_Compare_A(0,bit_size,*client_gm_, rand_state_);
    }

    return EncCompare_Helper(bit_size,server_->paillier(),comparator);
}

Rev_EncCompare_Owner Server_session::create_rev_enc_comparator_owner(size_t bit_size, COMPARISON_PROTOCOL comparison_prot)
{
    Comparison_protocol_A *comparator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator = new LSIC_A(0,bit_size,*client_gm_);
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator = new Compare_A(0,bit_size,*client_paillier_,*client_gm_,rand_state_);
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator = new GC_Compare_A(0,bit_size,*client_gm_, rand_state_);
    }
    
    return Rev_EncCompare_Owner(0,0,bit_size,*client_paillier_,comparator,rand_state_);
}


Rev_EncCompare_Helper Server_session::create_rev_enc_comparator_helper(size_t bit_size, COMPARISON_PROTOCOL comparison_prot)
{
    Comparison_protocol_B *comparator;
    
    if (comparison_prot == LSIC_PROTOCOL) {
        comparator = new LSIC_B(0,bit_size,server_->gm());
    }else if (comparison_prot == DGK_PROTOCOL){
        comparator = new Compare_B(0,bit_size,server_->paillier(),server_->gm());
    }else if (comparison_prot == GC_PROTOCOL) {
        comparator = new GC_Compare_B(0,bit_size,server_->gm(), rand_state_);
    }

    return Rev_EncCompare_Helper(bit_size,server_->paillier(),comparator);
}
