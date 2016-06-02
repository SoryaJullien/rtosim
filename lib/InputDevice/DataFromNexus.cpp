#include "rtosim/DataFromNexus.h"
#include "rtosim/ArrayConverter.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <string>
using std::string;
#include <vector>
using std::vector;
#include <iostream>
using std::cout;
using std::endl;
#include <OpenSim/OpenSim.h>
#include <thread>


std::string adapt(bool v) {

    std::string retStr("True");
    if (!v)
        retStr = "False";
    return retStr;
}

namespace rtosim {

    DataFromNexus::DataFromNexus(
        MarkerSetQueue& outputMarkerSetQueue,
        Concurrency::Latch& doneWithSubscriptions,
        Concurrency::Latch& doneWithExecution,
        FlowControl& runCondition,
        const std::string& osimFilename,
        const std::string& hostname) :
        useMarkerData_(true),
        outputMarkerSetQueue_(&outputMarkerSetQueue),
        useGrfData_(false),
        outputGrfQueue_(nullptr),
        doneWithSubscriptions_(doneWithSubscriptions),
        doneWithExecution_(doneWithExecution),
        runCondition_(runCondition),
        hostName_(hostname),
        fromNexusToModelLengthConversion_(1.),
        lastFrameNumberOfTheLoop_(0),
        previousFrameNumber_(0)
    {
        initialiseMarkerNames(osimFilename);
    }


    DataFromNexus::DataFromNexus(
        MultipleExternalForcesQueue& outputGrfQueue,
        Concurrency::Latch& doneWithSubscriptions,
        Concurrency::Latch& doneWithExecution,
        FlowControl& runCondition,
        const std::string& osimFilename,
        const std::string& hostname) :
        useMarkerData_(false),
        outputMarkerSetQueue_(nullptr),
        useGrfData_(true),
        outputGrfQueue_(&outputGrfQueue),
        doneWithSubscriptions_(doneWithSubscriptions),
        doneWithExecution_(doneWithExecution),
        runCondition_(runCondition),
        hostName_(hostname),
        fromNexusToModelLengthConversion_(1.),
        lastFrameNumberOfTheLoop_(0),
        previousFrameNumber_(0)
    {
        initialiseMarkerNames(osimFilename);
    }


    DataFromNexus::DataFromNexus(
        MarkerSetQueue& outputMarkerSetQueue,
        MultipleExternalForcesQueue& outputGrfQueue,
        Concurrency::Latch& doneWithSubscriptions,
        Concurrency::Latch& doneWithExecution,
        FlowControl& runCondition,
        const std::string& osimFilename,
        const std::string& hostname) : 
        useMarkerData_(true),
        outputMarkerSetQueue_(&outputMarkerSetQueue),
        useGrfData_(true),
        outputGrfQueue_(&outputGrfQueue),
        doneWithSubscriptions_(doneWithSubscriptions),
        doneWithExecution_(doneWithExecution),
        runCondition_(runCondition),
        hostName_(hostname),
        fromNexusToModelLengthConversion_(1.),
        lastFrameNumberOfTheLoop_(0),
        previousFrameNumber_(0)
    {
        initialiseMarkerNames(osimFilename);
    }

    void DataFromNexus::initialiseMarkerNames(const std::string& osimFilename) {

        OpenSim::Model model(osimFilename);
        OpenSim::Array<std::string> markerNamesArray;
        const_cast<OpenSim::MarkerSet&>(model.getMarkerSet()).getMarkerNames(markerNamesArray);
        rtosim::ArrayConverter::toStdVector(markerNamesArray, markerNamesFromModel_);
        if (model.getLengthUnits().getType() == OpenSim::Units::Meters)
            fromNexusToModelLengthConversion_ = 1. / 1000.;
    }

    void DataFromNexus::connectToServer(VDS::Client& client) const {
        //connect to server
        while (!client.IsConnected().Connected) {
            if (client.Connect(hostName_).Result != VDS::Result::Success) {
                cout << "Warning - connect failed..." << endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        cout << "Connected" << endl;
    }

    void DataFromNexus::initialiseDataToStream(VDS::Client& client) {

        if (useMarkerData_)
            client.EnableMarkerData();
        else
            client.DisableMarkerData();
        if (useGrfData_)
            client.EnableDeviceData();
        else
            client.DisableDeviceData();
        client.DisableSegmentData();
        client.DisableUnlabeledMarkerData();
        cout << "Marker Data Enabled: " << adapt(client.IsMarkerDataEnabled().Enabled) << endl;
        cout << "Device Data Enabled: " << adapt(client.IsDeviceDataEnabled().Enabled) << endl;
        cout << "Segments Data Enabled: " << adapt(client.IsSegmentDataEnabled().Enabled) << endl;
        cout << "Unlabelled Markers Data Enabled: " << adapt(client.IsUnlabeledMarkerDataEnabled().Enabled) << endl;

        while (client.SetStreamMode(VDS::StreamMode::ServerPush).Result != VDS::Result::Success)
            cout << ".";
        cout << endl;
        if (useMarkerData_)
            waitForCompleteFrame(client);
        if (useGrfData_)
            waitForForcePlates(client);
    }

    void DataFromNexus::waitForForcePlates(VDS::Client& client) {
        forcePlateCount_ = 0;
        while (forcePlateCount_ < 1 && runCondition_.getRunCondition()) {
            client.GetFrame();
            auto forcePlatesInfo = client.GetForcePlateCount();
            if (forcePlatesInfo.Result == VDS::Result::Success) {
                cout << "Waiting for devices..." << endl;
                forcePlateCount_ = forcePlatesInfo.ForcePlateCount;
            }
        }
        cout << "Found " << forcePlateCount_ << " force plates" << endl;

        for (unsigned i(0); i < client.GetDeviceCount().DeviceCount; ++i)
            if (client.GetDeviceName(i).DeviceType == VDS::DeviceType::ForcePlate) {
                std::string fpName = client.GetDeviceName(i).DeviceName;
                forcePlateNames_.push_back(fpName);
                cout << fpName << endl;
            }
    }

    void DataFromNexus::waitForCompleteFrame(VDS::Client& client) {

        vector<string> markerNamesFromNexus;
        bool isFrameComplete(false);
        do{
            if (client.GetFrame().Result == VDS::Result::Success) {
                unsigned subjectCount = client.GetSubjectCount().SubjectCount;
                if (subjectCount != 1)
                    cout << "Warning: " << subjectCount << " subject(s) in the capture volume" << endl;
                else {
                    unsigned subjectIndex(subjectCount - 1);
                    string subjectName = client.GetSubjectName(subjectIndex).SubjectName;
                    unsigned int markerCount = client.GetMarkerCount(subjectName).MarkerCount;
                    for (unsigned markerIndex(0); markerIndex < markerCount; ++markerIndex)
                        markerNamesFromNexus.push_back(client.GetMarkerName(subjectName, markerIndex).MarkerName);
                    isFrameComplete = true;
                } //end else
            }

            cout << ".";
        } while (!isFrameComplete);
        cout << "\nFound " << markerNamesFromNexus.size() << " markers" << endl;
    }


    void  DataFromNexus::getFrame(VDS::Client& client) {

        while (!(client.GetFrame().Result == VDS::Result::Success))
            cout << ".";
        unsigned currentFrameNumber = client.GetFrameNumber().FrameNumber;
        frameNumber_ = currentFrameNumber + lastFrameNumberOfTheLoop_;

        if (frameNumber_ < previousFrameNumber_ && increaseFrameNumbers_) {
            lastFrameNumberOfTheLoop_ += previousFrameNumber_;
            frameNumber_ = currentFrameNumber + lastFrameNumberOfTheLoop_;
        }

        previousFrameNumber_ = frameNumber_;
    }

    void DataFromNexus::pushMarkerData(VDS::Client& client) {

        auto rate = client.GetFrameRate();

        unsigned subjectCount = client.GetSubjectCount().SubjectCount;
        if (subjectCount != 1)
            cout << "Warning: " << subjectCount << " subject(s) in the capture volume" << endl;
        else {

            unsigned subjectIndex(subjectCount - 1);
            string subjectName = client.GetSubjectName(subjectIndex).SubjectName;
            unsigned int markerCount = client.GetMarkerCount(subjectName).MarkerCount;
            MarkerSetFrame currentFrame;

            currentFrame.data.resize(markerNamesFromModel_.size());
            for (unsigned markerIndex{ 0 }; markerIndex < markerCount; ++markerIndex) {

                string markerName = client.GetMarkerName(subjectName, markerIndex).MarkerName;
                VDS::Output_GetMarkerGlobalTranslation markerGlobalTranslation = client.GetMarkerGlobalTranslation(subjectName, markerName);
                MarkerData marker(
                    markerGlobalTranslation.Translation[0] * fromNexusToModelLengthConversion_,
                    markerGlobalTranslation.Translation[1] * fromNexusToModelLengthConversion_,
                    markerGlobalTranslation.Translation[2] * fromNexusToModelLengthConversion_
                    );
                marker.setOccluded(markerGlobalTranslation.Occluded);
                //to change with a rtosim::Mapper
                auto posIt = std::find(markerNamesFromModel_.begin(), markerNamesFromModel_.end(), markerName);
                if (posIt != markerNamesFromModel_.end())
                    currentFrame.data.at(std::distance(markerNamesFromModel_.begin(), posIt)) = marker;
            } //end for markerIndex

            //TODO: change this time
            currentFrame.time = 1. / rate.FrameRateHz*frameNumber_;
            outputMarkerSetQueue_->push(currentFrame);
         
        } //end subjectCount else
    }

    //this is a correction required to calculate the force plane moments 
    //in the correct reference system. They are the same values that are available through Vicon Nexus
    //but they have to be roated accordingly to client.SetAxisMapping
    vector<SimTK::Vec3> DataFromNexus::getForcePlatePosition() const {

        SimTK::Vec3 fpPos2(-.949, -.026, -.8724);
        SimTK::Vec3 fpPos1(-.949, -.026, -.3803);
        vector<SimTK::Vec3> pfPos{ fpPos1, fpPos2 };
        return pfPos;
    }

    void DataFromNexus::pushForcePlateData(VDS::Client& client) {

        vector<SimTK::Vec3> pfPos(getForcePlatePosition());

        auto rate = client.GetFrameRate();
        //Force Plates
        // Output the force plate information.
        const unsigned forcePlateSubsamples = client.GetForcePlateSubsamples(0).ForcePlateSubsamples;
        for (unsigned int ssIdx = 0; ssIdx < forcePlateSubsamples; ++ssIdx)    {
            MultipleExternalForcesFrame currentGrfFrame;
            currentGrfFrame.time = 1. / rate.FrameRateHz*(frameNumber_ + 1. / forcePlateSubsamples * ssIdx);
            for (unsigned fpIdx = 0; fpIdx < forcePlateCount_; ++fpIdx) {

                SimTK::Vec3 currentFpPos = pfPos.at(fpIdx);

                ExternalForceData fpData;
                fpData.setSourceName(forcePlateNames_.at(fpIdx));
                VDS::Output_GetGlobalForceVector forceVector = client.GetGlobalForceVector(fpIdx, ssIdx);
                SimTK::Vec3 grfVec(0.);
                grfVec[0] = forceVector.ForceVector[0];
                grfVec[1] = forceVector.ForceVector[1];
                grfVec[2] = forceVector.ForceVector[2];

                VDS::Output_GetGlobalCentreOfPressure centreOfPressure = client.GetGlobalCentreOfPressure(fpIdx, ssIdx);
                SimTK::Vec3 grfPoint;
                grfPoint[0] = centreOfPressure.CentreOfPressure[0]; // *fromNexusToModelLengthConversion_;
                grfPoint[1] = centreOfPressure.CentreOfPressure[1]; // *fromNexusToModelLengthConversion_;
                grfPoint[2] = centreOfPressure.CentreOfPressure[2]; // *fromNexusToModelLengthConversion_;

                //calculate the values of the moment of the 'position' reference system of the force plate in the global goordinate system
                SimTK::Vec3 momentOnPosition(0.);
                momentOnPosition[0] = currentFpPos[1] * grfVec[2] - currentFpPos[2] * grfVec[1];
                momentOnPosition[1] = currentFpPos[2] * grfVec[0] - currentFpPos[0] * grfVec[2];
                momentOnPosition[2] = currentFpPos[0] * grfVec[1] - currentFpPos[1] * grfVec[0];

                VDS::Output_GetGlobalMomentVector momentVector = client.GetGlobalMomentVector(fpIdx, ssIdx);

                //calculate the correct values of the moments relatively the global goordinate system by adding the missing position moment
                SimTK::Vec3 moments(0.);
                moments[0] = momentVector.MomentVector[0] + momentOnPosition[0];
                moments[1] = momentVector.MomentVector[1] + momentOnPosition[1];
                moments[2] = momentVector.MomentVector[2] + momentOnPosition[2];

                SimTK::Vec3 grfTorque;
                grfTorque[0] = 0;
                grfTorque[1] = (moments[1] - grfVec[0] * grfPoint[2] + grfVec[2] * grfPoint[0]);
                grfTorque[2] = 0;

                fpData.setForceVector(-grfVec);
                fpData.setMoments(-moments);
                fpData.setUseApplicationPoint(true);
                fpData.setTorque(-grfTorque);
                if (fabs(grfVec[1]) < 10) {
                    grfPoint[0] = 0; grfPoint[2] = 0;
                }

                fpData.setApplicationPoint(grfPoint);
                currentGrfFrame.data.push_back(fpData);
            }
            outputGrfQueue_->push(currentGrfFrame);
        }
    }

    void DataFromNexus::setAxisMapping(VDS::Client& client) const {

        //map axis, try Backward, right, up for subject walking against the y direction of the lab..
        //this is HARDCODED for the GU Undercroft Lab.. also, the mapping makes no sense IMO, 
        //I've used try and error approach to get the right one
        client.SetAxisMapping(
            VDS::Direction::Right,
            VDS::Direction::Up,
            VDS::Direction::Backward);
    }


    void DataFromNexus::operator()(){

        int numberOfFrame(0);
        int frameCounter(0);
        VDS::Client client;

        connectToServer(client);
        initialiseDataToStream(client);
        setAxisMapping(client);

        //receive the first full frame
        doneWithSubscriptions_.wait();

        auto start = std::chrono::system_clock::now();

        while (runCondition_.getRunCondition()) {

            getFrame(client);
            if (useMarkerData_)
                pushMarkerData(client);
            if (useGrfData_)
                pushForcePlateData(client);
        }

        client.DisableMarkerData();
        client.DisableDeviceData();
        cout << "Disconnecting from Nexus..." << std::endl;
        client.Disconnect();
        cout << "Disconnected" << std::endl;
        doneWithExecution_.wait();
    }

    DataFromNexus::~DataFromNexus() {

        cout << "Closing DataFromNexus..." << endl;
    }
}