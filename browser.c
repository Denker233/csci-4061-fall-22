/* CSCI-4061 Fall 2022
 * Group Member #1: Minrui Tian  tian0138
 * Group Member #2: Wanbo Geng   geng0098
 * Group Member #3: Zhipeng Cao  cao00223
 */

#include "wrapper.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>

#define MAX_TABS 100  // this gives us 99 tabs, 0 is reserved for the controller
#define MAX_BAD 1000
#define MAX_URL 100
#define MAX_FAV 100
#define MAX_LABELS 100 


comm_channel comm[MAX_TABS];         // Communication pipes 
char favorites[MAX_FAV][MAX_URL];    // Maximum char length of a url allowed
int num_fav = 0;                     // # favorites
int tab_num = 0;                     // tab number

typedef struct tab_list {
  int free;
  int pid; // may or may not be useful
} tab_list;

// Tab bookkeeping
tab_list TABS[MAX_TABS];  


/************************/
/* Simple tab functions */
/************************/

// return total number of tabs
int get_num_tabs () {
  return tab_num;

}

// get next free tab index
int get_free_tab () {
  for (int i=1; i<MAX_TABS; i++){
    if (TABS[i].free){
      return i;
    }
  }
  return -1;
}

// init TABS data structure
void init_tabs () {
  int i;

  for (i=1; i<MAX_TABS; i++)
    TABS[i].free = 1;
  TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) {
  if(on_favorites(uri)){ // not ok if uri is already on list or list is full
    alert("Favorite exists");
    return -1;
  }
  else if(num_fav>=MAX_FAV){
    alert("MAX FAV");
    return -1;
  }
  // else if(TABS[tab_index].free){ // check if free
  //   alert("Bad Tab");
  //   return -1;
  // }
  else{
    return 0;
  }
  
}


// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) {
  // Add uri to favorites file
  FILE* fd = fopen(".favorites", "a");
  if(fd==NULL){
    perror("Can't open file !");
  }
  // Update favorites array with the new favorite
  fprintf(fd, "%s\n", uri);//write to the file
  num_fav++;
  strcpy(favorites[num_fav-1], uri); //update favorite array
  fclose(fd);
}

// Set up favorites array
void init_favorites (char *fname) {
  FILE *fp = fopen(fname,"r");
  if(fp == NULL) {                  // check if open success
      perror("Error opening file");
   }
  while(fgets (favorites[num_fav], MAX_URL, fp) != NULL){//read favorite into buffer and replace the newline at the end
    int len = strlen(favorites[num_fav]);
    favorites[num_fav][len - 1] = '\0';
    num_fav++;
  }
  fclose(fp);
  return;
}

// Make fd non-blocking just as in class!
// Return 0 if ok, -1 otherwise
// Really a util but I want you to do it :-)
int non_block_pipe (int fd) {
  int flags;
  if((flags = fcntl(fd,F_GETFL,0)) <0){ //get flags
    return -1;
  }
  if((fcntl(fd,F_SETFL,flags|O_NONBLOCK))<0){ //add nonblock flag
    return -1;
  }
  return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

// Checks if tab is bad and url violates constraints; if so, return.
// Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
void handle_uri (char *uri, int tab_index) {
  req_t req;
  if(bad_format(uri)){ //check bad format
    alert("Bad Format");
    return;
  }

  if(on_blacklist(uri)){ //check blacklist
    alert("On blacklist");
    return;
  }

  if(!(tab_index>=1&&tab_index<MAX_TABS)||TABS[tab_index].free){ // check if free
    alert("Bad Tab");
    return;
  }

  sprintf(req.uri,"%s",uri); //set req attributes
  req.tab_index = tab_index;
  req.type=NEW_URI_ENTERED;
  if (write(comm[tab_index].inbound[1],&req,sizeof(req_t)) <0){
    perror("Failed to write uri into pipe.");
		return;
  }
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent (see Hint)
// Short function
void uri_entered_cb (GtkWidget* entry, gpointer data) {
  
  if(data == NULL) {	
    return;
  }


  

  // Get the tab (hint: wrapper.h)
  int tab_index = query_tab_id_for_request(entry,data);

  // Get the URL (hint: wrapper.h)
  char* uri = get_entered_uri(entry); 

  // Hint: now you are ready to handle_the_uri
  handle_uri(uri,tab_index);
}
  

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
// Long function
void new_tab_created_cb (GtkButton *button, gpointer data) {
  
  if (data == NULL) {
    return;
  }

  int new_tab_index;

  // at tab limit?
  if(get_num_tabs()>=MAX_TABS-1){
    alert("Exceed tab");
    return;
  }

  // Get a free tab
  new_tab_index = get_free_tab();

  // Create communication pipes for this tab
  if(pipe(comm[new_tab_index].inbound)<0){//controller to tab
    perror("Failed to pipe for inbound.");
		return;
  }   
  if(pipe(comm[new_tab_index].outbound)<0){// tab to controller
    perror("Failed to pipe for outbound.");
		return;
  }  

  // Make the read ends non-blocking 
  non_block_pipe(comm[new_tab_index].inbound[0]);
  non_block_pipe(comm[new_tab_index].outbound[0]);

  
  // fork and create new render tab
  // Note: render has different arguments now: tab_index, both pairs of pipe fd's
  // (inbound then outbound) -- this last argument will be 4 integers "a b c d"
  // Hint: stringify args
  
  pid_t child_pid = fork();
  
  if(child_pid==0){ // fork a child to execl render
    char num[5];
    char str_bound[50];
    sprintf(num,"%d",new_tab_index);           //convert them to string
    sprintf(str_bound,"%d %d %d %d",comm[new_tab_index].inbound[0],comm[new_tab_index].inbound[1],comm[new_tab_index].outbound[0],comm[new_tab_index].outbound[1]); 
    execl("./render","render",num,str_bound,NULL);
  }
  else if(child_pid>0){// Controller parent just does some TABS bookkeeping
    TABS[new_tab_index].free = 0;
    TABS[new_tab_index].pid = child_pid;
    tab_num++;
    return;
  }
  else{
    perror("Fork Fail!");
    exit(1);
  }

  
}

// This is called when a favorite is selected for rendering in a tab
// Hint: you will use handle_uri ...
// However: you will need to first add "https://" to the uri so it can be rendered
// as favorites strip this off for a nicer looking menu
// Short
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) {

  if (data == NULL) {
    return;
  }
  
  // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
  // get basic uri
  char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));

  // append "https://" for rendering
  char uri[MAX_URL];
  sprintf(uri, "https://%s", basic_uri);

  // Get the tab (hint: wrapper.h)
  int tab_index = query_tab_id_for_request(menu_item,data);

  // Hint: now you are ready to handle_the_uri
  handle_uri(uri,tab_index);
  return;
}


// BIG CHANGE: the controller now runs an loop so it can check all pipes
// Long function
int run_control() {
  browser_window * b_window = NULL;
  int i, nRead;
  req_t req;
  int status;

  //Create controller window
  create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
		 G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

  // Create favorites menu
  create_browser_menu(&b_window, &favorites, num_fav);
  tab_num++;                                           //tab for controller;
  while (1) {
    process_single_gtk_event();

    // Read from all tab pipes including private pipe (index 0)
    // to handle commands:
    // PLEASE_DIE (controller should die, self-sent): send PLEASE_DIE to all tabs
    // From any tab:
    //    IS_FAV: add uri to favorite menu (Hint: see wrapper.h), and update .favorites
    //    TAB_IS_DEAD: tab has exited, what should you do?

    // Loop across all pipes from VALID tabs -- starting from 0
    for (i=0; i<MAX_TABS; i++) {
      if (TABS[i].free) continue;
      nRead = read(comm[i].outbound[0], &req, sizeof(req_t));

      // Check that nRead returned something before handling cases
      if(nRead<0){
        continue;
      }
      // Case 1: PLEASE_DIE
      if(req.type==PLEASE_DIE){ // terminate all tabs
        for (i=1; i<MAX_TABS; i++){
          if (TABS[i].free){ //jump through unopened tab
            continue;
          }
          req.tab_index = i;
          if(write(comm[i].inbound[1],&req,sizeof(req_t))<0){//write PLEASE_DIE to opened tabs
            perror("Failed to write Please_Die into pipe.");
            exit(1);
          } 
          tab_num--;   // number bookkeeping
          if(wait(&status)<0){//dellocate resource for all children
            perror("Unable to dellocate resource for children");
            exit(1);
          }
        }
        exit(0);
      }

      // Case 2: TAB_IS_DEAD
      if(req.type==TAB_IS_DEAD){
        if(wait(&status)<0){//dellocate resources for the specific tab when its X is hit
          perror("Unable to dellocate resource for children when X is hit for a tab");
          exit(0);
        } 
        TABS[req.tab_index].free = 1; //tab bookkeeping
        tab_num--;
      }
	    
      // Case 3: IS_FAV
      if(req.type==IS_FAV){  // add favorite to the list and update the file
        if(fav_ok(req.uri)==0){
        add_uri_to_favorite_menu(b_window,req.uri);
        update_favorites_file(req.uri);
        }
      }
    }
    usleep(1000);  //sleep to reduce unnecessary cpu use
  }
  return 0;
}


int main(int argc, char **argv)
{

  if (argc != 1) {
    fprintf (stderr, "browser <no_args>\n");
    exit (0);
  }
  int status;

  init_tabs ();
  // init blacklist (see util.h), and favorites (write this, see above)
  init_blacklist(".blacklist");
  init_favorites(".favorites");

  // Fork controller
  // Child creates a pipe for itself comm[0]
  // then calls run_control ()
  // Parent waits ...
  pid_t ch_pid = fork();

  if(ch_pid==0){ //fork a child to run controller
    if(pipe(comm[0].outbound)<0){
      perror("Failed to create pipe for controller.");
		  return -1;
    }
    non_block_pipe(comm[0].outbound[0]); //unblock pipe for controller
    run_control();
  }
  else if (ch_pid>0){ // parent
    if(wait(&status)<0){//wait for controller  and dellocate resource 
      perror("Unable to dellocate resource for controller");
      exit(0);
    }
  }
  else{
    perror("Fork fail for controller");
    exit(1);
  }
  return 0;

}
