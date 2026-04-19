import { Entity, PrimaryColumn, Column } from 'typeorm';

/**
 * IoTClient class for database access
 */
@Entity('IoTClient')
export default class IoTClient {
  @PrimaryColumn()
    id: string;

  @Column({ name: 'client_type' })
    clientType: string;

  @Column({ name: 'iot_hub_name' })
    iotHubName: string;

  @Column()
    token: string;

  @Column({ name: 'token_spent' })
    tokenSpent: boolean;

  @Column({ name: 'unsecured_ip' })
    unsecuredIp: string;

  @Column({ name: 'is_active' })
    isActive: boolean;

  @Column({ name: 'site_id' })
    siteId: string;

  @Column()
    name: string;

  @Column('jsonb')
    last_log: string;

  @Column('jsonb')
    front_matter: string;

  @Column('jsonb')
    realtime: string;

  @Column('jsonb')
    settings: string;

  @Column('timestamp with time zone')
    time_stamp: Date;

  @Column({ name: 'time_zone' })
    time_zone: string;
}
