// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#include <Ice/Application.h>
#include <Glacier/Glacier.h>
#include <TestCommon.h>
#include <CallbackI.h>
#include <IceUtil/Base64.h>
#include <Ice/System.h>
#include <Ice/SslExtension.h>

using namespace std;
using namespace Ice;

class CallbackClient : public Application
{
public:

    virtual int run(int, char*[]);
};

int
main(int argc, char* argv[])
{
    CallbackClient app;
    return app.main(argc, argv);
}

int
CallbackClient::run(int argc, char* argv[])
{
    string ref;

    ObjectAdapterPtr adapter = communicator()->createObjectAdapterWithEndpoints("CallbackReceiverAdapter", "default");
    adapter->activate();
    // Put the print statement after activate(), so that if
    // Ice.PrintAdapterReady is set, the "ready" is the first output
    // from the client, and not the print statement below. Otherwise
    // the Python test scripts will be confused, as they expect the
    // "ready" from the Object Adapter to be the first thing that is
    // printed.
    cout << "creating and activating callback receiver adapter... " << flush;
    cout << "ok" << endl;

    cout << "creating and adding callback receiver object... " << flush;
    CallbackReceiverI* callbackReceiverImpl = new CallbackReceiverI;
    ObjectPtr callbackReceiver = callbackReceiverImpl;
    adapter->add(callbackReceiver, stringToIdentity("callbackReceiver"));
    cout << "ok" << endl;

    cout << "testing stringToProxy for glacier starter... " << flush;
    ref = "Glacier/starter:default -p 12346 -t 5000";
    ObjectPrx starterBase = communicator()->stringToProxy(ref);
    cout << "ok" << endl;

    cout << "testing checked cast for glacier starter... " << flush;
    Glacier::StarterPrx starter = Glacier::StarterPrx::checkedCast(starterBase);
    test(starter);
    cout << "ok" << endl;

    ByteSeq privateKey;
    ByteSeq publicKey;
    ByteSeq routerCert;

    cout << "starting up glacier router... " << flush;
    Glacier::RouterPrx router;
    try
    {
	router = starter->startRouter("dummy", "abc123", privateKey, publicKey, routerCert);
    }
    catch (const Glacier::CannotStartRouterException& ex)
    {
	cerr << appName() << ": " << ex << ":\n" << ex.reason << endl;
	return EXIT_FAILURE;
    }
    catch (const Glacier::InvalidPasswordException& ex)
    {
	cerr << appName() << ": " << ex << endl;
	return EXIT_FAILURE;
    }

    PropertiesPtr properties = communicator()->getProperties();

    string clientConfig = properties->getProperty("Ice.SSL.Client.Config");
    string serverConfig = properties->getProperty("Ice.SSL.Server.Config");

    if (!clientConfig.empty() && !serverConfig.empty())
    {
        string privateKeyBase64 = IceUtil::Base64::encode(privateKey);
        string publicKeyBase64  = IceUtil::Base64::encode(publicKey);
        string routerCertString = IceUtil::Base64::encode(routerCert);

        IceSSL::SystemPtr sslSystem = communicator()->getSslSystem();
        IceSSL::SslExtensionPtr sslExtension = communicator()->getSslExtension();

        // Configure server, client is already configured.
        sslSystem->configure(IceSSL::Server);
        sslSystem->setCertificateVerifier(IceSSL::ClientServer, sslExtension->getSingleCertVerifier(routerCert));

        // Set the keys overrides.
        sslSystem->setRSAKeysBase64(IceSSL::ClientServer, privateKeyBase64, publicKeyBase64);
        sslSystem->addTrustedCertificateBase64(IceSSL::ClientServer, routerCertString);
    }

    test(router);
    cout << "ok" << endl;

    cout << "pinging glacier router... " << flush;
    router->ice_ping();
    cout << "ok" << endl;

    cout << "installing glacier router... " << flush;
    communicator()->setDefaultRouter(router);
    adapter->addRouter(router);
    cout << "ok" << endl;

    cout << "testing stringToProxy... " << flush;
    ref = "callback:tcp -p 12345 -t 5000";
    ObjectPrx base = communicator()->stringToProxy(ref);
    cout << "ok" << endl;

    cout << "testing checked cast... " << flush;
    CallbackPrx twoway = CallbackPrx::checkedCast(base->ice_twoway()->ice_timeout(-1)->ice_secure(false));
    test(twoway);
    cout << "ok" << endl;

    CallbackReceiverPrx twowayR = CallbackReceiverPrx::uncheckedCast(
	adapter->createProxy(stringToIdentity("callbackReceiver")));
	
    {
	cout << "testing oneway callback... " << flush;
	CallbackPrx oneway = CallbackPrx::uncheckedCast(twoway->ice_oneway());
	CallbackReceiverPrx onewayR = CallbackReceiverPrx::uncheckedCast(twowayR->ice_oneway());
	Context context;
	context["_fwd"] = "o";
	oneway->initiateCallback(onewayR, context);
	test(callbackReceiverImpl->callbackOK());
	cout << "ok" << endl;
    }

    {
	cout << "testing twoway callback... " << flush;
	Context context;
	context["_fwd"] = "t";
	twoway->initiateCallback(twowayR, context);
	test(callbackReceiverImpl->callbackOK());
	cout << "ok" << endl;
    }

    {
	cout << "ditto, but with user exception... " << flush;
	Context context;
	context["_fwd"] = "t";
	try
	{
	    twoway->initiateCallbackEx(twowayR, context);
	    test(false);
	}
	catch (const CallbackException& ex)
	{
	    test(ex.someValue == 3.14);
	    test(ex.someString == "3.14");
	}
	test(callbackReceiverImpl->callbackOK());
	cout << "ok" << endl;
    }

    cout << "testing server shutdown... " << flush;
    twoway->shutdown();
    // No ping, otherwise the glacier router prints a warning
    // message if it's started with --Ice.ConnectionWarnings.
    cout << "ok" << endl;
    /*
    try
    {
	twoway->ice_ping();
	test(false);
    }
    // If we use the glacier router, the exact exception reason gets
    // lost.
    //catch(const ConnectFailedException&)
    catch(const UnknownLocalException&)
    {
	cout << "ok" << endl;
    }
    */

    cout << "shutting down glacier router... " << flush;
    router->shutdown();
    try
    {
	router->ice_ping();
	test(false);
    }
    catch(const ConnectFailedException&)
    {
	cout << "ok" << endl;
    }

    return EXIT_SUCCESS;
}
