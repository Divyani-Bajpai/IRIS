/* Demonstration data only. Replace this provider when the real IRIS data adapter is integrated. */
window.IRIS_SAMPLE_DATA = {
  metrics: [
 {key:'cpu_percent',name:'CPU utilization',icon:'⌁',avg:15,sd:12,value:24.8,unit:'%',source:'default',digits:1},
 {key:'available_ram_mb',name:'Available RAM',icon:'▥',avg:5734,sd:2457,value:5324,unit:'MB',source:'default',digits:0},
 {key:'page_faults_per_sec',name:'Page faults / sec',icon:'↯',avg:45,sd:40,value:39,unit:'/s',source:'default',digits:0},
 {key:'disk_queue_length',name:'Disk queue length',icon:'▤',avg:.2,sd:.3,value:.18,unit:'',source:'default',digits:2},
 {key:'percent_disk_time',name:'Disk time',icon:'◴',avg:10,sd:15,value:18,unit:'%',source:'default',digits:1}
],
  processes: [{name:'browser.exe',memory:'842 MB',cpu:'8.4%',status:'Resource-heavy'},{name:'Code.exe',memory:'612 MB',cpu:'5.1%',status:'Moderate'},{name:'System',memory:'188 MB',cpu:'1.2%',status:'Normal'},{name:'explorer.exe',memory:'146 MB',cpu:'0.8%',status:'Normal'},{name:'SearchIndexer.exe',memory:'94 MB',cpu:'2.3%',status:'Moderate'}],
  startup: [{name:'CloudSync',scope:'HKCU · Run',note:'User startup entry'},{name:'SecurityTray',scope:'HKLM · Run',note:'Machine startup entry'},{name:'AudioService',scope:'HKLM · Run',note:'Machine startup entry'},{name:'ChatClient',scope:'HKCU · Run',note:'User startup entry'},{name:'GraphicsHelper',scope:'HKLM · Run',note:'Machine startup entry'},{name:'UpdateAgent',scope:'HKCU · Run',note:'User startup entry'}]
};
