/*
 * Copyright 2013-2015 Raphael Bost
 *
 * This file is part of ciphermed.

 *  ciphermed is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  ciphermed is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with ciphermed.  If not, see <http://www.gnu.org/licenses/>. 2
 *
 */

#include <assert.h>
#include <vector>
#include <mpc/lsic.hh>
#include <mpc/garbled_comparison.hh>
#include <mpc/enc_comparison.hh>
#include <mpc/rev_enc_comparison.hh>
#include <mpc/enc_argmax.hh>
#include <mpc/linear_enc_argmax.hh>
#include <mpc/tree_enc_argmax.hh>
#include <mpc/change_encryption_scheme.hh>

#include <crypto/gm.hh>
#include <NTL/ZZ.h>
#include <util/util.hh>
#include <math/util_gmp_rand.h>
#include <mpc/private_comparison.hh>
#include <functional>

#include <FHE.h>
#include <EncryptedArray.h>

#include<iostream>

using namespace std;
using namespace NTL;


static void test_lsic(unsigned int nbits = 256)
{
    cout << "Test LSIC ..." << endl;
    ScopedTimer timer("LSIC");

    ScopedTimer *t;
    t = new ScopedTimer("LSIC init");


    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    mpz_class a, b;
    mpz_urandom_len(a.get_mpz_t(), randstate, nbits);
    mpz_urandom_len(b.get_mpz_t(), randstate, nbits);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    LSIC_B party_b(b, nbits, gm_priv);
    LSIC_A party_a(a, nbits, gm);

    delete t;

    t = new ScopedTimer("LSIC execution");

    runProtocol(party_a, party_b, randstate);

    bool result = party_b.gm().decrypt(party_a.output());

    delete t;

    assert( result == (a < b));

    cout << "Test LSIC passed" << endl;
}

static void test_compare(unsigned int nbits = 256)
{
    cout << "Test compare ..." << endl;
    ScopedTimer timer("Compare");

    ScopedTimer *t;
    t = new ScopedTimer("Compare init");


    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    mpz_class a, b;
    mpz_urandom_len(a.get_mpz_t(), randstate, nbits);
    mpz_urandom_len(b.get_mpz_t(), randstate, nbits);

//    cout << "a = " << a.get_str(2) << endl;
//    cout << "b = " << b.get_str(2) << endl;

    Compare_A party_a(a, nbits, p, gm, randstate);
    Compare_B party_b(b, nbits, pp, gm_priv);

    delete t;

    t = new ScopedTimer("Compare execution");

    runProtocol(party_a, party_b,randstate);

    bool result = party_b.gm().decrypt(party_a.output());

    delete t;

    assert( result == (a < b));

    cout << "Test Compare passed" << endl;
}

static void test_gc(unsigned int nbits = 256)
{
//    nbits = 128;
    cout << "Test compare with Garbled Circuits ..." << endl;
    ScopedTimer timer("GC Compare");

    ScopedTimer *t;
    t = new ScopedTimer("GC Compare init");


    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    mpz_class a, b;
    mpz_urandomb(a.get_mpz_t(), randstate, nbits);
    mpz_urandomb(b.get_mpz_t(), randstate, nbits);

    //    cout << "a = " << a.get_str(2) << endl;
    //    cout << "b = " << b.get_str(2) << endl;

    GC_Compare_A party_a(a, nbits, gm, randstate);
    GC_Compare_B party_b(b, nbits, gm_priv, randstate);

    delete t;

    t = new ScopedTimer("GC Compare execution");

    runProtocol(party_a, party_b,randstate);

    bool result = party_b.gm().decrypt(party_a.output());

    delete t;
    assert( result == (a < b));

    cout << "Test GC Compare passed" << endl;

}

static void test_enc_compare(unsigned int nbits = 256,unsigned int lambda = 100)
{
    cout << "Test comparison over encrypted data ..." << endl;
    ScopedTimer timer("Enc. Compare");

    ScopedTimer *t;
    t = new ScopedTimer("Protocol init");

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    mpz_class a, b;
    mpz_urandom_len(a.get_mpz_t(), randstate, nbits);
    mpz_urandom_len(b.get_mpz_t(), randstate, nbits);

    LSIC_A party_a(0, nbits, gm);
    LSIC_B party_b(0, nbits, gm_priv);

    EncCompare_Owner client(pp.encrypt(a),pp.encrypt(b), nbits, p, &party_b, randstate);
    EncCompare_Helper server(nbits,pp, &party_a);

    delete t;

    t = new ScopedTimer("Running protocol");

    runProtocol(client,server,randstate,lambda);

    delete t;

    bool result = client.output();

    assert( result == (a <= b));

    cout << "Test passed" << endl;
}

static void test_rev_enc_compare(unsigned int nbits = 256,unsigned int lambda = 100)
{
    cout << "Test reverse comparison over encrypted data ..." << endl;
    ScopedTimer timer("Enc. Compare");

    ScopedTimer *t;
    t = new ScopedTimer("Protocol init");

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    LSIC_A party_a(0, nbits, gm);
    LSIC_B party_b(0, nbits, gm_priv);

    mpz_class a, b;
    mpz_urandom_len(a.get_mpz_t(), randstate, nbits);
    mpz_urandom_len(b.get_mpz_t(), randstate, nbits);

    Rev_EncCompare_Helper server(nbits,pp,&party_b);
    Rev_EncCompare_Owner client(pp.encrypt(a),pp.encrypt(b), nbits, p,&party_a, randstate);

    delete t;

    t = new ScopedTimer("Running protocol");

    runProtocol(client,server,randstate,lambda);

    delete t;

    bool result = server.output();

    assert( result == (a <= b));

    cout << "Test passed" << endl;
}

static void test_enc_argmax(unsigned int k = 5, unsigned int nbits = 256,unsigned int lambda = 100, unsigned int num_threads = 1)
{
    cout << "Test argmax over encrypted data ..." << endl;
    cout << k << " integers of " << nbits << " bits, " << lambda << " bits of security\n";
    ScopedTimer timer("Enc. Argmax");

    vector<mpz_class> v(k);

    ScopedTimer *t, *timer_exec;
    t = new ScopedTimer("Protocol init");

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    size_t real_argmax = 0;
    for (size_t i = 0; i < k; i++) {
        mpz_urandom_len(v[i].get_mpz_t(), randstate, nbits);
        if (v[i] > v[real_argmax]) {
            real_argmax = i;
        }
    }

    for (size_t i = 0; i < k; i++) {
        v[i] = pp.encrypt(v[i]);
    }

//    auto party_a_creator = [&gm,&p,nbits,&randstate](){ return new Compare_A(0,nbits,p,gm,randstate); };
//    auto party_b_creator = [&gm_priv,&pp,nbits](){ return new Compare_B(0,nbits,pp,gm_priv); };
    auto party_a_creator = [&gm,nbits](){ return new LSIC_A(0,nbits,gm); };
    auto party_b_creator = [&gm_priv,nbits](){ return new LSIC_B(0,nbits,gm_priv); };

    timer_exec = new ScopedTimer("Protocol execution");

    EncArgmax_Owner client(v,nbits,p,party_a_creator,randstate);
    EncArgmax_Helper server(nbits,k,pp,party_b_creator);

    delete t;

    t = new ScopedTimer("Running comparisons");

    if (num_threads > 1) {
        runProtocol(client,server,randstate,lambda,num_threads);
    }else{
        runProtocol(client,server,randstate,lambda);
    }
    delete t;
    delete timer_exec;
    vector<mpz_class>::iterator argmax;

    size_t mpc_argmax = client.output();

    assert(real_argmax == mpc_argmax);
}

static void test_linear_enc_argmax(unsigned int k = 5, unsigned int nbits = 256,unsigned int lambda = 100)
{
    cout << "Test linear argmax over encrypted data ..." << endl;
    cout << k << " integers of " << nbits << " bits, " << lambda << " bits of security\n";
    ScopedTimer timer("Linear Enc. Argmax");

    vector<mpz_class> v(k);

    ScopedTimer *t, *timer_exec;
    t = new ScopedTimer("Protocol init");

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    size_t real_argmax = 0;
    for (size_t i = 0; i < k; i++) {
        mpz_urandom_len(v[i].get_mpz_t(), randstate, nbits);
        if (v[i] > v[real_argmax]) {
            real_argmax = i;
        }
    }

    for (size_t i = 0; i < k; i++) {
        v[i] = pp.encrypt(v[i]);
    }

//    auto party_a_creator = [&gm,&p,nbits,&randstate](){ return new Compare_A(0,nbits,p,gm,randstate); };
//    auto party_b_creator = [&gm_priv,&pp,nbits](){ return new Compare_B(0,nbits,pp,gm_priv); };
    auto party_a_creator = [&gm,nbits](){ return new LSIC_A(0,nbits,gm); };
    auto party_b_creator = [&gm_priv,nbits](){ return new LSIC_B(0,nbits,gm_priv); };


    Linear_EncArgmax_Owner client(v,nbits,p,randstate, lambda);
    Linear_EncArgmax_Helper server(nbits,k,pp);

    delete t;

    timer_exec = new ScopedTimer("Protocol execution");

    runProtocol(client,server,party_a_creator, party_b_creator, randstate,lambda);

    delete timer_exec;

    vector<mpz_class>::iterator argmax;

    size_t mpc_argmax = client.output();

//    cout << "Real argmax = " << real_argmax;
//    cout << "\nFound argmax = " << mpc_argmax << endl;
    assert(real_argmax == mpc_argmax);
}

static void test_tree_enc_argmax(unsigned int k = 5, unsigned int nbits = 256,unsigned int lambda = 100)
{
    cout << "Test tree argmax over encrypted data ..." << endl;
    cout << k << " integers of " << nbits << " bits, " << lambda << " bits of security\n";
    ScopedTimer timer("Tree Enc. Argmax");

    vector<mpz_class> v(k);

    ScopedTimer *t, *timer_exec;
    t = new ScopedTimer("Protocol init");

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_p = Paillier_priv_fast::keygen(randstate,1024);
    Paillier_priv_fast pp(sk_p,randstate);
    Paillier p(pp.pubkey(),randstate);

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    size_t real_argmax = 0;
    for (size_t i = 0; i < k; i++) {
        mpz_urandom_len(v[i].get_mpz_t(), randstate, nbits);
//        v[i] = i;
        if (v[i] > v[real_argmax]) {
            real_argmax = i;
        }
    }

    for (size_t i = 0; i < k; i++) {
        v[i] = pp.encrypt(v[i]);
    }

    //    auto party_a_creator = [&gm,&p,nbits,&randstate](){ return new Compare_A(0,nbits,p,gm,randstate); };
    //    auto party_b_creator = [&gm_priv,&pp,nbits](){ return new Compare_B(0,nbits,pp,gm_priv); };
    auto party_a_creator = [&gm,nbits](){ return new LSIC_A(0,nbits,gm); };
    auto party_b_creator = [&gm_priv,nbits](){ return new LSIC_B(0,nbits,gm_priv); };


    Tree_EncArgmax_Owner client(v,nbits,p,randstate, lambda);
    Tree_EncArgmax_Helper server(nbits,k,pp);

    delete t;

    timer_exec = new ScopedTimer("Protocol execution");

    runProtocol(client,server,party_a_creator, party_b_creator, randstate,lambda);

    delete timer_exec;

    vector<mpz_class>::iterator argmax;

    size_t mpc_argmax = client.output();

    cout << "Real argmax = " << real_argmax;
    cout << "\nFound argmax = " << mpc_argmax << endl;
    assert(real_argmax == mpc_argmax);
}

/*
static ZZX makeIrredPoly(long p, long d)
{
    assert(d >= 1);
    assert(ProbPrime(p));

    if (d == 1) return ZZX(1, 1); // the monomial X

    zz_pBak bak; bak.save();
    zz_p::init(p);
    return to_ZZX(BuildIrred_zz_pX(d));
}
*/

static void test_change_ES()
{
    long p = 2;
    long r = 1;
    long d = 1;
    long c = 2;

    long L = 2;


    long w = 64;
    long s = 1;
    long k = 80;
    long chosen_m = 0; // XXX: check?

    long m = FindM(k, L, c, p, d, s, chosen_m, true);

    FHEcontext context(m, p, r);
    buildModChain(context, L, c);
    FHESecKey secretKey(context);
    const FHEPubKey& publicKey = secretKey;
    secretKey.GenSecKey(w); // A Hamming-weight-w secret key

    ZZX G;
    if (d == 0)
    G = context.alMod.getFactorsOverZZ()[0];
    else
    G = makeIrredPoly(p, d);

    EncryptedArray ea(context, G);

    gmp_randstate_t randstate;
    gmp_randinit_default(randstate);
    gmp_randseed_ui(randstate,time(NULL));

    auto sk_gm = GM_priv::keygen(randstate);
    GM_priv gm_priv(sk_gm,randstate);
    GM gm(gm_priv.pubkey(),randstate);

    size_t n_slots = ea.size();
    vector<long> bits_query(n_slots);

    vector<mpz_class> c_gm(bits_query.size());

    for (size_t i = 0; i < c_gm.size(); i++) {
        bits_query[i] = gmp_urandomb_ui(randstate,1);
        c_gm[i] = gm.encrypt(bits_query[i]);
    }

    Change_ES_FHE_to_GM_slots_A switcher;
    vector<mpz_class> c_gm_blinded = switcher.blind(c_gm,gm,randstate, ea.size());

    Ctxt c_blinded_fhe = Change_ES_FHE_to_GM_slots_B::decrypt_encrypt(c_gm_blinded,gm_priv,publicKey,ea);

    Ctxt c_fhe = switcher.unblind(c_blinded_fhe,publicKey,ea);

    vector<long> result;
    ea.decrypt(c_fhe, secretKey, result);

    for (size_t i = 0; i < bits_query.size(); i++) {
        assert(bits_query[i] == result[i]);
    }
}


static void usage(char *prog)
{
    cerr << "Usage: "<<prog<<" [ optional parameters ]...\n";
    cerr << "  optional parameters have the form 'attr1=val1 attr2=val2 ...'\n";
    cerr << "  e.g, 'lambda=100'\n\n";
    cerr << "  lambda is the parameter for statistical indistinguishability [default=100]\n";
    cerr << "  l is the number of bits of tested integers [default=256]\n";
    cerr << "  n is the number of elements in the argmax test [default=10]\n";
    cerr << "  t is the number of threads used by the protocols (only for argmax test) [default=1]\n";
    cerr << endl;
    exit(0);
}


int main(int argc, char **argv)
{
    if (argc != 5)
    {
        std::cerr << "Usage: test_mpc <lambda> <l> <n> <t>" << std::endl;
        return 1;
    }
    unsigned int lambda = atoi(argv[1]);
    unsigned int l = atoi(argv[2]);
    unsigned int n = atoi(argv[3]);
    unsigned int t = atoi(argv[4]);

    SetSeed(to_ZZ(time(NULL)));
    srand(time(NULL));


    test_lsic(l);
    test_compare(l);

    for (int i = 0; i < 1; i++) {
        test_gc(l);
        cout << "\n\n";
    }


    test_enc_compare(l,lambda);
    cout << "\n\n";
    test_rev_enc_compare(l,lambda);

    cout << "\n\n";
    test_enc_argmax(n,l,lambda,t);
    cout << "\n\n";
    test_linear_enc_argmax(n,l,lambda);
    cout << "\n\n";
    test_tree_enc_argmax(n,l,lambda);

    cout << "\n\n";
    test_change_ES();
	return 0;
}
