#include "tsc_service.h"
#include <chrono>

namespace traffic_signal_controller_service {
    
    std::mutex dpp_mtx;

    bool tsc_service::initialize() {
        try
        {
            // Intialize spat kafka producer
            std::string bootstrap_server = streets_service::streets_configuration::get_string_config("bootstrap_server");
            std::string spat_topic_name = streets_service::streets_configuration::get_string_config("spat_producer_topic");

            std::string dpp_consumer_topic = streets_service::streets_configuration::get_string_config("desired_phase_plan_consumer_topic");
            std::string dpp_consumer_group = streets_service::streets_configuration::get_string_config("desired_phase_plan_consumer_group");
            
            if (!spat_producer && !initialize_kafka_producer(bootstrap_server, spat_topic_name, spat_producer)) {
                
                SPDLOG_ERROR("Failed to initialize kafka spat_producer!");
                return false;
                
            }

            if (!desired_phase_plan_consumer && !initialize_kafka_consumer(bootstrap_server, dpp_consumer_topic, dpp_consumer_group, desired_phase_plan_consumer)) {
                
                SPDLOG_ERROR("Failed to initialize kafka desired_phase_plan_consumer!");
                return false;
                
            }            
            // Initialize SNMP Client
            std::string target_ip = streets_service::streets_configuration::get_string_config("target_ip");
            int target_port = streets_service::streets_configuration::get_int_config("target_port");
            std::string community = streets_service::streets_configuration::get_string_config("community");
            int snmp_version = streets_service::streets_configuration::get_int_config("snmp_version");
            int timeout = streets_service::streets_configuration::get_int_config("snmp_timeout");
            if (!snmp_client_ptr && !initialize_snmp_client(target_ip, target_port, community, snmp_version, timeout)) {    
                SPDLOG_ERROR("Failed to initialize snmp_client!");
                return false;
            }
            
            //  Initialize tsc configuration state kafka producer
            std::string tsc_config_topic_name = streets_service::streets_configuration::get_string_config("tsc_config_producer_topic");
            if (!tsc_config_producer && !initialize_kafka_producer(bootstrap_server, tsc_config_topic_name, tsc_config_producer)) {

                SPDLOG_ERROR("Failed to initialize kafka tsc_config_producer!");
                return false;
            }
            //Initialize TSC State
            use_desired_phase_plan_update_ = streets_service::streets_configuration::get_boolean_config("use_desired_phase_plan_update");            
            if (!initialize_tsc_state(snmp_client_ptr)){
                SPDLOG_ERROR("Failed to initialize tsc state");
                return false;
            }
            tsc_config_state_ptr = tsc_state_ptr->get_tsc_config_state();
            // Initialize spat_worker
            std::string socket_ip = streets_service::streets_configuration::get_string_config("udp_socket_ip");
            int socket_port = streets_service::streets_configuration::get_int_config("udp_socket_port");
            int socket_timeout = streets_service::streets_configuration::get_int_config("socket_timeout");
            bool use_msg_timestamp =  streets_service::streets_configuration::get_boolean_config("use_tsc_timestamp");         
            enable_snmp_cmd_logging_ = streets_service::streets_configuration::get_boolean_config("enable_snmp_cmd_logging");

            if (!initialize_spat_worker(socket_ip, socket_port, socket_timeout, use_msg_timestamp)) {
                SPDLOG_ERROR("Failed to initialize SPaT Worker");
                return false;
            }

            if (!initialize_intersection_client()) {
                SPDLOG_ERROR("Failed to initialize intersection client");
                return false;
            }
            // Add all phases to a single map
            auto all_phases = tsc_state_ptr->get_vehicle_phase_map();
            auto ped_phases = tsc_state_ptr->get_ped_phase_map();
            // Insert pedestrian phases into map of vehicle phases.
            all_phases.insert(ped_phases.begin(), ped_phases.end());
            // Initialize spat ptr
            initialize_spat(intersection_client_ptr->get_intersection_name(), intersection_client_ptr->get_intersection_id(), 
                                all_phases);
            
            control_tsc_state_sleep_dur_ = streets_service::streets_configuration::get_int_config("control_tsc_state_sleep_duration");
            
            // Initialize monitor desired phase plan
            monitor_dpp_ptr = std::make_shared<monitor_desired_phase_plan>( snmp_client_ptr );

            // Initialize control_tsc_state ptr
            control_tsc_state_ptr_ = std::make_shared<control_tsc_state>(snmp_client_ptr, tsc_state_ptr);

            if (enable_snmp_cmd_logging_)
            {
                configure_snmp_cmd_logger();
            }

            SPDLOG_INFO("Traffic Signal Controller Service initialized successfully!");
            return true;
        }
        catch (const streets_service::streets_configuration_exception &ex)
        {
            SPDLOG_ERROR("Signal Optimization Service Initialization failure: {0} ", ex.what());
            return false;
        }
    }

    bool tsc_service::initialize_kafka_producer(const std::string &bootstrap_server, const std::string &producer_topic,
         std::shared_ptr<kafka_clients::kafka_producer_worker> &producer) {
        
        auto client = std::make_unique<kafka_clients::kafka_client>();
        producer = client->create_producer(bootstrap_server, producer_topic);
        if (!producer->init())
        {
            SPDLOG_CRITICAL("Kafka producer initialize error on topic {0}", producer_topic);
            return false;
        }
        SPDLOG_DEBUG("Initialized Kafka producer on topic {0}!", producer_topic);
        return true;
    }

    bool tsc_service::initialize_kafka_consumer(const std::string &bootstrap_server, 
                                                const std::string &consumer_topic,  
                                                const std::string &consumer_group, 
                                                std::shared_ptr<kafka_clients::kafka_consumer_worker> &kafka_consumer) {
        auto client = std::make_unique<kafka_clients::kafka_client>();
        kafka_consumer = client->create_consumer(bootstrap_server, consumer_topic, consumer_group);
        if (!kafka_consumer->init())
        {
            SPDLOG_CRITICAL("Kafka initialize error");
            return false;
        }
        SPDLOG_DEBUG("Initialized Kafka consumer!");
        return true;
    }

    bool tsc_service::initialize_snmp_client(const std::string &server_ip, const int server_port, const std::string &community,
                                        const int snmp_version, const int timeout) {
        try {
            snmp_client_ptr = std::make_shared<snmp_client>(server_ip, server_port, community, snmp_version, timeout);
            SPDLOG_DEBUG("SNMP Client initialized!");
            return true;
        }
        catch (const snmp_client_exception &e) {
            SPDLOG_ERROR("Exception encountered initializing snmp client : \n {0}", e.what());
            return false;
        }
    }

    bool tsc_service::initialize_tsc_state(const std::shared_ptr<snmp_client> _snmp_client_ptr ) {
        tsc_state_ptr = std::make_shared<tsc_state>(_snmp_client_ptr );
        if ( !tsc_state_ptr->initialize() ){
            SPDLOG_ERROR("Failed to initialize tsc_state!");
            return false;
        }
        SPDLOG_DEBUG("TSC State initialized!");
        return true;
    }
    bool tsc_service::enable_spat() const{
        // Enable SPaT 
        snmp_response_obj enable_spat;
        enable_spat.type = snmp_response_obj::response_type::INTEGER;
        enable_spat.val_int = 2;
        if (!snmp_client_ptr->process_snmp_request(ntcip_oids::ENABLE_SPAT_OID, request_type::SET, enable_spat)){
            SPDLOG_ERROR("Failed to enable SPaT broadcasting on Traffic Signal Controller!");
            return false;
        }
        SPDLOG_DEBUG("Enabled UDP broadcasting of NTCIP SPaT data from TSC!");
        return true;
    }

    bool tsc_service::initialize_spat_worker(const std::string &udp_socket_ip, const int udp_socket_port, 
                                        const int timeout, const bool use_tsc_timestamp) {
        spat_worker_ptr = std::make_shared<spat_worker>(udp_socket_ip, udp_socket_port, timeout, use_tsc_timestamp);
        // HTTP request to update intersection information
        if (!spat_worker_ptr->initialize())
        {
            SPDLOG_ERROR("Failed to initialize spat worker!");
            return false;
        }
        SPDLOG_DEBUG("Spat worker initialized successfully!");
        return true;
    }

    bool tsc_service::initialize_intersection_client() {
        intersection_client_ptr = std::make_shared<intersection_client>();
        if ( !intersection_client_ptr->request_intersection_info() ) {
            SPDLOG_ERROR("Failed to retrieve intersection information from intersection model!");
            return false;
        }
        SPDLOG_DEBUG("Intersection client initialized successfully!");
        return true;
    }
    
    void tsc_service::initialize_spat(const std::string &intersection_name, const int intersection_id, 
                                const std::unordered_map<int,int> &phase_number_to_signal_group) {
        spat_ptr =  std::make_shared<signal_phase_and_timing::spat>();
        spat_ptr->initialize_intersection( intersection_name, intersection_id, phase_number_to_signal_group);
    }


    void tsc_service::produce_spat_json() const {
        try {
            int count = 0;
            uint64_t spat_latency = 0;
            while(spat_worker_ptr && tsc_state_ptr && spat_producer) {
                try {
                    spat_worker_ptr->receive_spat(spat_ptr);
                    SPDLOG_DEBUG("Current SPaT : {0} ", spat_ptr->toJson());   
                    if(!use_desired_phase_plan_update_){
                       // throws monitor_states_exception
                        tsc_state_ptr->add_future_movement_events(spat_ptr);
                        
                    }else{
                        std::scoped_lock<std::mutex> lck{dpp_mtx};
                         // throws desired phase plan exception when no previous green information present
                        monitor_dpp_ptr->update_spat_future_movement_events(spat_ptr, tsc_state_ptr); 
                    }
                    if (spat_ptr && spat_producer) {
                        spat_producer->send(spat_ptr->toJson());
                    }
                    // Sample size for spat latency measurement 20 mgs
                    if (count <= 20 ) {
                        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                        spat_latency += timestamp - spat_ptr->get_intersection().get_epoch_timestamp();
                        count++;
                    } else {
                        double latency_measure = ((double)(spat_latency))/20.0;
                        SPDLOG_WARN("SPat average latency over 2 seconds is {0} ms and total latency for 20 messages is {1} ms!", latency_measure, spat_latency);
                        spat_latency = 0;
                        count = 0;
                    }
                }
                catch( const signal_phase_and_timing::signal_phase_and_timing_exception &e ) {
                    SPDLOG_ERROR("Encountered exception, spat not published : \n {0}", e.what());
                }
                catch( const traffic_signal_controller_service::monitor_desired_phase_plan_exception &e) {
                    SPDLOG_ERROR("Could not update movement events, spat not published. Encountered exception : \n {0}", e.what());
                }
                catch(const traffic_signal_controller_service::monitor_states_exception &e){
                    SPDLOG_ERROR("Could not update movement events, spat not published. Encountered exception : \n {0}", e.what());
                }   
    } 
            SPDLOG_WARN("Stopping produce_spat_json! Are pointers null: spat_worker {0}, spat_producer {1}, tsc_state {2}",
                spat_worker_ptr == nullptr, spat_ptr == nullptr, tsc_state_ptr == nullptr);
        }
        catch( const udp_socket_listener_exception &e) {
            SPDLOG_ERROR("Encountered exception : \n {0}", e.what());
        }
        
    }

    void tsc_service::produce_tsc_config_json() const{
        try {
            
            while(tsc_config_producer->is_running() && tsc_config_state_ptr )
            { 
                tsc_config_producer->send(tsc_config_state_ptr->toJson());
                std::this_thread::sleep_for(std::chrono::milliseconds(10000)); // Sleep for 10 second between publish   
            }
        }
        catch( const streets_tsc_configuration::tsc_configuration_state_exception &e) {
            SPDLOG_ERROR("Encountered exception : \n {0}", e.what());
        }
    }

    void tsc_service::consume_desired_phase_plan() {
        desired_phase_plan_consumer->subscribe();
        while (desired_phase_plan_consumer->is_running())
        {
            const std::string payload = desired_phase_plan_consumer->consume(1000);
            if (payload.length() != 0)
            {
                SPDLOG_DEBUG("Consumed: {0}", payload);
                std::scoped_lock<std::mutex> lck{dpp_mtx};
                monitor_dpp_ptr->update_desired_phase_plan(payload);
                
                // update command queue
                if(monitor_dpp_ptr->get_desired_phase_plan_ptr()){
                    // Send desired phase plan to control_tsc_state
                    control_tsc_state_ptr_->update_tsc_control_queue(monitor_dpp_ptr->get_desired_phase_plan_ptr(), tsc_set_command_queue_);
                    
                }
            }

        }        
    }

    void tsc_service::control_tsc_phases()
    {
        try{
            while(true)
            {
                set_tsc_hold_and_omit();
                std::this_thread::sleep_for(std::chrono::milliseconds(control_tsc_state_sleep_dur_));
            }
        }
        catch(const control_tsc_state_exception &e){
            SPDLOG_ERROR("Encountered exception : \n {0}", e.what());
            SPDLOG_ERROR("Removing front command from queue :  {0}", tsc_set_command_queue_.front().get_cmd_info());
            tsc_set_command_queue_.pop();

        }

    }
    
    void tsc_service::set_tsc_hold_and_omit()
    {

        while(!tsc_set_command_queue_.empty())
        {
            //Check if event is expired
            auto event_execution_start_time = std::chrono::milliseconds(tsc_set_command_queue_.front().start_time_);
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(event_execution_start_time - std::chrono::system_clock::now().time_since_epoch());
            if(duration.count() < 0){
                throw control_tsc_state_exception("SNMP set command is expired! Start time was " 
                    + std::to_string(event_execution_start_time.count()) + " and current time is " 
                    + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".");
            }
            std::this_thread::sleep_for(duration);

            if(!(tsc_set_command_queue_.front()).run())
            {
                throw control_tsc_state_exception("Could not set state for movement group in desired phase plan");
            }
            // Log command info sent
            SPDLOG_INFO(tsc_set_command_queue_.front().get_cmd_info());

            
            if ( enable_snmp_cmd_logging_ ){
                if(auto logger = spdlog::get("snmp_cmd_logger"); logger != nullptr ){
                    logger->info( tsc_set_command_queue_.front().get_cmd_info());
                }
            }

            tsc_set_command_queue_.pop();
        }
    }

    void tsc_service::configure_snmp_cmd_logger() const
    {
        try{
            auto snmp_cmd_logger  = spdlog::daily_logger_mt<spdlog::async_factory>(
                "snmp_cmd_logger",  // logger name
                    streets_service::streets_configuration::get_string_config("snmp_cmd_log_path")+
                    streets_service::streets_configuration::get_string_config("snmp_cmd_log_filename") +".log",  // log file name and path
                    23, // hours to rotate
                    59 // minutes to rotate
                );
            // Only log log statement content
            snmp_cmd_logger->set_pattern("[%H:%M:%S:%e ] %v");
            snmp_cmd_logger->set_level(spdlog::level::info);
            snmp_cmd_logger->flush_on(spdlog::level::info);
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            spdlog::error( "Log initialization failed: {0}!",ex.what());
        }
    }
    

    void tsc_service::start() {
        
        std::thread tsc_config_thread(&tsc_service::produce_tsc_config_json, this);

        std::thread spat_t(&tsc_service::produce_spat_json, this);

        std::thread desired_phase_plan_t(&tsc_service::consume_desired_phase_plan, this);

        std::thread control_phases_t(&tsc_service::control_tsc_phases, this);
        
        // Run threads as joint so that they dont overlap execution 
        tsc_config_thread.join();
        spat_t.join();
        desired_phase_plan_t.join();
        control_phases_t.join();
    }
    

    tsc_service::~tsc_service()
    {
        if (spat_producer)
        {
            SPDLOG_WARN("Stopping spat producer!");
            spat_producer->stop();
        }

        if(tsc_config_producer)
        {
            SPDLOG_WARN("Stopping tsc config producer!");
            tsc_config_producer->stop();
        }
        if (desired_phase_plan_consumer) {
            SPDLOG_WARN("Stopping desired phase plan consumer!");
            desired_phase_plan_consumer->stop();
        }
    }
}