#ifndef rtosim_DataFromNexus_h
#define rtosim_DataFromNexus_h

#include <string>
#include <thread>
#include <memory>
#include <OpenSim/OpenSim.h>
#include <Client.h> //from ViconSDK
#include "rtosim/Mapper.h"
#include "rtosim/concurrency/Concurrency.h"
#include "rtosim/queue/MultipleExternalForcesQueue.h"
#include "rtosim/queue/MarkerSetQueue.h"
#include "rtosim/FlowControl.h"

namespace VDS = ViconDataStreamSDK::CPP;
namespace rtosim {

    class DataFromNexus{

    public:
        DataFromNexus() = delete;
        DataFromNexus(DataFromNexus&) = delete;

        DataFromNexus(
            MarkerSetQueue& outputMarkerSetQueue,
            Concurrency::Latch& doneWithSubscriptions,
            Concurrency::Latch& doneWithExecution,
            FlowControl& runCondition,
            const std::string& osimFilename,
            const std::string& hostname = "127.0.0.1:801");

        DataFromNexus(
            MultipleExternalForcesQueue& outputGrfQueue,
            Concurrency::Latch& doneWithSubscriptions,
            Concurrency::Latch& doneWithExecution,
            FlowControl& runCondition,
            const std::string& osimFilename,
            const std::string& hostname = "127.0.0.1:801");

        DataFromNexus(
            MarkerSetQueue& outputMarkerSetQueue,
            MultipleExternalForcesQueue& outputGrfQueue,
            Concurrency::Latch& doneWithSubscriptions,
            Concurrency::Latch& doneWithExecution,
            FlowControl& runCondition,
            const std::string& osimFilename,
            const std::string& hostname = "127.0.0.1:801");


        void setLoopMode(bool isLoop) {
            increaseFrameNumbers_ = isLoop;
        }
        virtual ~DataFromNexus();
        void operator()();

    private:
        
        std::vector<SimTK::Vec3> getForcePlatePosition() const;
        void setAxisMapping(VDS::Client& client) const;
        void initialiseMarkerNames(const std::string&);
        void initialiseDataToStream(VDS::Client& client);
        void waitForForcePlates(VDS::Client& client);
        void waitForCompleteFrame(VDS::Client& client);
        void connectToServer(VDS::Client& client) const;
        void getFrame(VDS::Client& client);
        void pushMarkerData(VDS::Client& client);
        void pushForcePlateData(VDS::Client& client);
        MarkerSetQueue* outputMarkerSetQueue_;
        MultipleExternalForcesQueue* outputGrfQueue_;
        Concurrency::Latch& doneWithSubscriptions_;
        Concurrency::Latch& doneWithExecution_;
        FlowControl& runCondition_;
        std::string hostName_;
        std::vector<std::string> markerNamesFromModel_;
        std::vector<std::string> forcePlateNames_;
        //    SimTK::Vec3 originTraslation_;
        double fromNexusToModelLengthConversion_;
        bool useMarkerData_, useGrfData_;
        unsigned forcePlateCount_;
        bool increaseFrameNumbers_;
        unsigned previousFrameNumber_, frameNumber_, lastFrameNumberOfTheLoop_;
    };
}

#endif
