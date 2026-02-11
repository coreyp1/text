#include <gtest/gtest.h>

extern "C" {
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_core.h>
}

static GTEXT_YAML_Status noop_cb(GTEXT_YAML_Stream *s, const void *evp, void *user) {
    (void)s; (void)evp; (void)user; 
    return GTEXT_YAML_OK;
}

// Test 1: Docker Compose file structure
TEST(YamlRealWorld, DockerCompose) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "version: '3.8'\n"
        "services:\n"
        "  web:\n"
        "    image: nginx:latest\n"
        "    ports:\n"
        "      - '80:80'\n"
        "      - '443:443'\n"
        "    volumes:\n"
        "      - ./html:/usr/share/nginx/html\n"
        "    environment:\n"
        "      NGINX_HOST: example.com\n"
        "      NGINX_PORT: 80\n"
        "  db:\n"
        "    image: postgres:13\n"
        "    environment:\n"
        "      POSTGRES_PASSWORD: secret\n"
        "      POSTGRES_USER: admin\n"
        "      POSTGRES_DB: mydb\n"
        "    volumes:\n"
        "      - db-data:/var/lib/postgresql/data\n"
        "volumes:\n"
        "  db-data:\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 2: GitHub Actions workflow
TEST(YamlRealWorld, GitHubActions) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "name: CI\n"
        "on:\n"
        "  push:\n"
        "    branches: [main, develop]\n"
        "  pull_request:\n"
        "    branches: [main]\n"
        "jobs:\n"
        "  build:\n"
        "    runs-on: ubuntu-latest\n"
        "    steps:\n"
        "      - uses: actions/checkout@v2\n"
        "      - name: Set up Node\n"
        "        uses: actions/setup-node@v2\n"
        "        with:\n"
        "          node-version: '16'\n"
        "      - name: Install dependencies\n"
        "        run: npm ci\n"
        "      - name: Run tests\n"
        "        run: npm test\n"
        "      - name: Build\n"
        "        run: npm run build\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 3: Kubernetes deployment manifest
TEST(YamlRealWorld, KubernetesDeployment) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "apiVersion: apps/v1\n"
        "kind: Deployment\n"
        "metadata:\n"
        "  name: nginx-deployment\n"
        "  labels:\n"
        "    app: nginx\n"
        "spec:\n"
        "  replicas: 3\n"
        "  selector:\n"
        "    matchLabels:\n"
        "      app: nginx\n"
        "  template:\n"
        "    metadata:\n"
        "      labels:\n"
        "        app: nginx\n"
        "    spec:\n"
        "      containers:\n"
        "      - name: nginx\n"
        "        image: nginx:1.14.2\n"
        "        ports:\n"
        "        - containerPort: 80\n"
        "        resources:\n"
        "          limits:\n"
        "            memory: 128Mi\n"
        "            cpu: 500m\n"
        "          requests:\n"
        "            memory: 64Mi\n"
        "            cpu: 250m\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 4: Ansible playbook
TEST(YamlRealWorld, AnsiblePlaybook) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "- name: Configure web servers\n"
        "  hosts: webservers\n"
        "  become: yes\n"
        "  vars:\n"
        "    http_port: 80\n"
        "    max_clients: 200\n"
        "  tasks:\n"
        "    - name: Install Apache\n"
        "      apt:\n"
        "        name: apache2\n"
        "        state: present\n"
        "        update_cache: yes\n"
        "    - name: Start Apache\n"
        "      service:\n"
        "        name: apache2\n"
        "        state: started\n"
        "        enabled: yes\n"
        "    - name: Copy config file\n"
        "      template:\n"
        "        src: httpd.conf.j2\n"
        "        dest: /etc/apache2/apache2.conf\n"
        "      notify:\n"
        "        - restart apache\n"
        "  handlers:\n"
        "    - name: restart apache\n"
        "      service:\n"
        "        name: apache2\n"
        "        state: restarted\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 5: Application configuration file
TEST(YamlRealWorld, AppConfig) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "app:\n"
        "  name: MyApplication\n"
        "  version: 1.2.3\n"
        "  debug: false\n"
        "server:\n"
        "  host: 0.0.0.0\n"
        "  port: 8080\n"
        "  ssl:\n"
        "    enabled: true\n"
        "    cert: /path/to/cert.pem\n"
        "    key: /path/to/key.pem\n"
        "database:\n"
        "  driver: postgresql\n"
        "  host: db.example.com\n"
        "  port: 5432\n"
        "  name: production\n"
        "  pool:\n"
        "    min: 5\n"
        "    max: 20\n"
        "    idle_timeout: 300\n"
        "logging:\n"
        "  level: info\n"
        "  format: json\n"
        "  outputs:\n"
        "    - stdout\n"
        "    - /var/log/app.log\n"
        "cache:\n"
        "  enabled: true\n"
        "  backend: redis\n"
        "  host: cache.example.com\n"
        "  ttl: 3600\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 6: Travis CI configuration
TEST(YamlRealWorld, TravisCI) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "language: python\n"
        "python:\n"
        "  - '3.8'\n"
        "  - '3.9'\n"
        "  - '3.10'\n"
        "install:\n"
        "  - pip install -r requirements.txt\n"
        "  - pip install pytest coverage\n"
        "script:\n"
        "  - pytest --cov=mypackage\n"
        "after_success:\n"
        "  - coveralls\n"
        "notifications:\n"
        "  email:\n"
        "    recipients:\n"
        "      - dev@example.com\n"
        "    on_success: change\n"
        "    on_failure: always\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 7: OpenAPI/Swagger specification
TEST(YamlRealWorld, OpenAPI) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "openapi: 3.0.0\n"
        "info:\n"
        "  title: Sample API\n"
        "  description: Optional description\n"
        "  version: 1.0.0\n"
        "servers:\n"
        "  - url: https://api.example.com/v1\n"
        "paths:\n"
        "  /users:\n"
        "    get:\n"
        "      summary: List users\n"
        "      responses:\n"
        "        '200':\n"
        "          description: Success\n"
        "          content:\n"
        "            application/json:\n"
        "              schema:\n"
        "                type: array\n"
        "                items:\n"
        "                  type: object\n"
        "                  properties:\n"
        "                    id:\n"
        "                      type: integer\n"
        "                    name:\n"
        "                      type: string\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 8: Helm values file
TEST(YamlRealWorld, HelmValues) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "replicaCount: 3\n"
        "image:\n"
        "  repository: myapp\n"
        "  pullPolicy: IfNotPresent\n"
        "  tag: 'latest'\n"
        "service:\n"
        "  type: ClusterIP\n"
        "  port: 80\n"
        "ingress:\n"
        "  enabled: true\n"
        "  className: nginx\n"
        "  annotations:\n"
        "    cert-manager.io/cluster-issuer: letsencrypt\n"
        "  hosts:\n"
        "    - host: example.com\n"
        "      paths:\n"
        "        - path: /\n"
        "          pathType: Prefix\n"
        "  tls:\n"
        "    - secretName: example-tls\n"
        "      hosts:\n"
        "        - example.com\n"
        "resources:\n"
        "  limits:\n"
        "    cpu: 500m\n"
        "    memory: 512Mi\n"
        "  requests:\n"
        "    cpu: 250m\n"
        "    memory: 256Mi\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 9: Ruby on Rails database configuration
TEST(YamlRealWorld, RailsDatabase) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "default: &default\n"
        "  adapter: postgresql\n"
        "  encoding: unicode\n"
        "  pool: 5\n"
        "  timeout: 5000\n"
        "development:\n"
        "  <<: *default\n"
        "  database: myapp_development\n"
        "  username: devuser\n"
        "  password: devpass\n"
        "test:\n"
        "  <<: *default\n"
        "  database: myapp_test\n"
        "production:\n"
        "  <<: *default\n"
        "  database: myapp_production\n"
        "  username: produser\n"
        "  password: <%= ENV['DATABASE_PASSWORD'] %>\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 10: Jenkins pipeline configuration
TEST(YamlRealWorld, JenkinsPipeline) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "pipeline:\n"
        "  agent: any\n"
        "  stages:\n"
        "    - stage: Build\n"
        "      steps:\n"
        "        - checkout: scm\n"
        "        - sh: make build\n"
        "    - stage: Test\n"
        "      steps:\n"
        "        - sh: make test\n"
        "        - junit: '**/target/*.xml'\n"
        "    - stage: Deploy\n"
        "      when:\n"
        "        branch: main\n"
        "      steps:\n"
        "        - sh: |\n"
        "            echo 'Deploying...'\n"
        "            ./deploy.sh production\n"
        "  post:\n"
        "    always:\n"
        "      - cleanWs\n"
        "    success:\n"
        "      - mail:\n"
        "          to: team@example.com\n"
        "          subject: Build Success\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 11: AWS CloudFormation template (simplified)
TEST(YamlRealWorld, CloudFormation) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "AWSTemplateFormatVersion: '2010-09-09'\n"
        "Description: Simple EC2 instance\n"
        "Parameters:\n"
        "  InstanceType:\n"
        "    Type: String\n"
        "    Default: t2.micro\n"
        "    AllowedValues:\n"
        "      - t2.micro\n"
        "      - t2.small\n"
        "      - t2.medium\n"
        "Resources:\n"
        "  MyEC2Instance:\n"
        "    Type: AWS::EC2::Instance\n"
        "    Properties:\n"
        "      ImageId: ami-0c55b159cbfafe1f0\n"
        "      InstanceType:\n"
        "        Ref: InstanceType\n"
        "      Tags:\n"
        "        - Key: Name\n"
        "          Value: MyInstance\n"
        "Outputs:\n"
        "  InstanceId:\n"
        "    Description: Instance ID\n"
        "    Value:\n"
        "      Ref: MyEC2Instance\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}

// Test 12: Complex nested configuration with mixed styles
TEST(YamlRealWorld, MixedStyles) {
    GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
    GTEXT_YAML_Stream *s = gtext_yaml_stream_new(&opts, noop_cb, NULL);
    ASSERT_NE(s, nullptr);
    
    const char *yaml = 
        "config:\n"
        "  inline_map: {key1: value1, key2: value2}\n"
        "  inline_list: [item1, item2, item3]\n"
        "  block_map:\n"
        "    nested1: value1\n"
        "    nested2: value2\n"
        "  block_list:\n"
        "    - item1\n"
        "    - item2\n"
        "  mixed:\n"
        "    - {a: 1, b: 2}\n"
        "    - {c: 3, d: 4}\n"
        "  multiline: |\n"
        "    This is a\n"
        "    multiline string\n"
        "    with literal style\n"
        "  folded: >\n"
        "    This is a folded\n"
        "    string that will\n"
        "    be joined\n";
    
    GTEXT_YAML_Status st = gtext_yaml_stream_feed(s, yaml, strlen(yaml));
    EXPECT_EQ(st, GTEXT_YAML_OK);
    st = gtext_yaml_stream_finish(s);
    EXPECT_EQ(st, GTEXT_YAML_OK);
    gtext_yaml_stream_free(s);
}
