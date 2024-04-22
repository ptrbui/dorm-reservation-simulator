## Project Components

### `Client.cpp` (Client Application)
- **Functionality**: Handles user input for login credentials and encrypts the information before sending to the main server. Provides interface for viewing book availability.
- **Key Features**:
  - User login authentication.
  - Encryption of login credentials.
  - Interaction with the main server for book availability.

### `ServerM.cpp` (Main Server Application)
- **Role**: Acts as the main server (ServerM) in the system.
- **Responsibilities**:
  - Verifying member identities.
  - Coordinating communications between clients and department servers.

### `ServerS.cpp` (Science Department Server)
- **Purpose**: Manages the Science department's book database.
- **Functions**:
  - Maintaining records of Science books.
  - Handling requests from the main server about Science department books.

### `ServerL.cpp` (Literature Department Server)
- **Function**: Implements the server for the Literature department.
- **Key Tasks**:
  - Manages Literature books' information.
  - Responds to main server queries regarding Literature books.

### `ServerH.cpp` (History Department Server)
- **Description**: Dedicated server for the History department.
- **Activities**:
  - Maintains data on History books.
  - Provides information to the main server about History books.
